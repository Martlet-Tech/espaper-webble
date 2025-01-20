/**
 * @file http_server.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "bsp.h"
#include "utils.h"
#include "img_proc.h"
#include "esp_event.h"
#include "yepd.h"

#define BUFFER_SIZE 1024
#define MAX_FILE_SIZE (4 * 1024 * 1024) // 假设文件大小最大为 2MB

extern YEPD *epd;

typedef struct {
	char *data; // 用于存储文件数据的 PSRAM 缓存
	size_t size; // 当前缓存大小
	size_t offset; // 当前写入的偏移量
} psram_buffer_t;

static const char *TAG = "http_server";

static char *html_cache = NULL; // PSRAM 中的缓存指针
static size_t html_cache_size = 0; // 缓存的大小

bool is_busy = false;

extern char processing_stage[];

static int find_jpeg_start(const unsigned char *data, size_t data_len)
{
	// 定义Content-Type字段和分隔符的标记
	const char *content_type = "Content-Type: image/jpeg";
	const char *double_crlf = "\r\n\r\n";

	// 查找Content-Type的结束位置
	const unsigned char *pos =
		memmem(data, data_len, content_type, strlen(content_type));
	if (pos == NULL) {
		printf("Content-Type not found\n");
		return -1;
	}

	// 查找Content-Type行后的双换行符位置
	pos = memmem(pos + strlen(content_type),
		     data_len - (pos - data + strlen(content_type)),
		     double_crlf, strlen(double_crlf));
	if (pos == NULL) {
		printf("Double CRLF not found after Content-Type\n");
		return -1;
	}

	// JPEG数据开始的位置是双换行符的末尾
	return (pos - data) + strlen(double_crlf);
}

/* 根路径处理函数 */
static esp_err_t index_get_handler(httpd_req_t *req)
{
	FILE *f = fopen(SPIFFS_MOUNT_POINT "/index.html", "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Unable to open index.html file.");
		httpd_resp_send_404(req);
		return ESP_FAIL;
	}

	fseek(f, 0, SEEK_END);
	size_t file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	html_cache =
		heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM); // 分配 PSRAM
	if (html_cache == NULL) {
		ESP_LOGE(TAG, "Failed to allocate memory for HTML cache.");
		fclose(f);
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}
	show_ram_space("index_get_handler");

	fread(html_cache, 1, file_size, f); // 读取文件到缓存
	fclose(f);

	html_cache_size = file_size; // 保存缓存大小
	ESP_LOGI(TAG, "Html loaded into PSRAM, size: %d", file_size);

	httpd_resp_send(req, html_cache, html_cache_size); // 发送响应

	free(html_cache);

	return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
	if (is_busy) {
		ESP_LOGI(TAG, "Server busy");
		httpd_resp_set_type(req, "application/json");
		const char *busy_resp =
			"{\"code\":503, \"msg\":\"Server busy. Please try again later.\"}";
		httpd_resp_send(req, busy_resp, strlen(busy_resp));
		return ESP_OK;
	}

	// 标志置为忙碌状态
	is_busy = true;

	char buf[BUFFER_SIZE];
	int received;
	size_t offset = 0;
	size_t remaining_size = MAX_FILE_SIZE;

	ESP_LOGI(TAG, "httpd_req_t *req->method= %d", req->method);
	ESP_LOGI(TAG, "httpd_req_t *req.uri= %s", req->uri);
	ESP_LOGI(TAG, "httpd_req_t *req.content_len= %d", req->content_len);

	strcpy(processing_stage, "saving");

	// 分配 PSRAM 缓存
	char *psram_data = heap_caps_malloc(MAX_FILE_SIZE, MALLOC_CAP_SPIRAM);
	if (!psram_data) {
		ESP_LOGE(TAG, "Failed to allocate PSRAM buffer");
		goto upload_post_handler_error;
	}

	ESP_LOGI(TAG, "Receiving file and storing to PSRAM...");

	// 循环接收并存储到 PSRAM
	while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
		// 检查剩余空间是否足够
		if (offset + received > MAX_FILE_SIZE) {
			ESP_LOGE(TAG,
				 "Not enough PSRAM space to store the file");
			heap_caps_free(psram_data);
			return ESP_FAIL;
		}

		// 将接收到的数据拷贝到 PSRAM 中
		memcpy(psram_data + offset, buf, received);
		offset += received;
		remaining_size -= received;

		printf(".");
		fflush(stdout); // 手动刷新缓冲区
	}
	printf("\r\n");

	if (received < 0) {
		ESP_LOGE(TAG, "File upload failed");
		goto upload_post_handler_error;
	}

	// find file start
	ptrdiff_t offset_file_start =
		find_jpeg_start((const unsigned char *)(psram_data), 1024);

	const void *pos = NULL;

	// find file ends
	const char *end_string = "------WebKitFormBoundary";
	// Adjust search size
	size_t search_size = req->content_len > 1024 ? 1024 : req->content_len;

	pos = memmem(psram_data + req->content_len - search_size, search_size,
		     end_string, strlen(end_string));
	ptrdiff_t offset_file_end = (const unsigned char *)pos -
				    (const unsigned char *)(psram_data);
	offset_file_end -= 2;
	ESP_LOGI(TAG, "File offset = %d", (int)offset_file_end);

	if ((offset_file_end < offset_file_start) || (offset_file_start < 0)) {
		ESP_LOGI(TAG, "File offset file fail");
		// Send success response in JSON format
		httpd_resp_set_type(req, "application/json");
		const char *resp_str =
			"{\"code\":500, \"msg\":\"Upload data parse fail.\"}";
		httpd_resp_send(req, resp_str, strlen(resp_str));

		goto upload_post_handler_error;
	}

	ESP_LOGI(TAG, "File upload successful, total size: %zu bytes", offset);

	// Send success response in JSON format
	httpd_resp_set_type(req, "application/json");
	const char *resp_str = "{\"code\":200, \"msg\":\"Upload complete.\"}";
	httpd_resp_send(req, resp_str, strlen(resp_str));

	sdcard_save_buff((uint8_t *)(psram_data), offset,
			 SDCARD_MOUNT_POINT "/request.bin");

	int current_maxnum_jpg = scan_and_sort_images();
	int new_img_num = get_file_num_from_index(current_maxnum_jpg - 1) + 1;
	char jpg_file_path[64]; // 确保这个长度足够存储路径字符串
	snprintf(jpg_file_path, sizeof(jpg_file_path), "%s/%d.jpg",
		 SDCARD_MOUNT_POINT, new_img_num);

	ESP_LOGW(TAG, "image save as %s", jpg_file_path);

	sdcard_save_buff((uint8_t *)(psram_data + offset_file_start),
			 offset_file_end - offset_file_start, jpg_file_path);
	set_current_image_number(new_img_num);

	// 释放 PSRAM 缓存
	free(psram_data);
	strcpy(processing_stage, "decoding");

	display_jpg_file(epd, jpg_file_path);

	is_busy = false;
	ESP_LOGI(TAG, "upload_post_handler return OK");
	return ESP_OK;

upload_post_handler_error:
	is_busy = false;
	if (psram_data)
		heap_caps_free(psram_data);
	return ESP_FAIL;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
	// 发送空的响应或图标文件
	httpd_resp_send(req, "", 0); // 发送空响应
	return ESP_OK;
}

static esp_err_t lang_handler(httpd_req_t *req)
{
	const char *file_path = (const char *)req->user_ctx;

	FILE *file = fopen(file_path, "r");
	if (!file) {
		ESP_LOGE(TAG, "Failed to open file: %s", file_path);
		httpd_resp_send_404(req);
		return ESP_FAIL;
	}

	// 设置 Content-Type 为 UTF-8
	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Content-Type",
			   "application/json; charset=utf-8");

	char buffer[512];
	size_t read_bytes;
	while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
			fclose(file);
			httpd_resp_send_500(req);
			return ESP_FAIL;
		}
	}

	fclose(file);
	httpd_resp_send_chunk(req, NULL, 0); // 结束块发送
	return ESP_OK;
}

static esp_err_t device_info_handler(httpd_req_t *req)
{
	// 动态生成 JSON 数据
	char response[128];
	snprintf(response, sizeof(response),
		 "{\"name\":\"%s\", \"width\":\"%d\", \"height\":\"%d\"}",
		 epd->name, epd->width, epd->height);

	// 设置响应头
	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

// 启动 HTTP 服务器
void start_http_server()
{
	httpd_handle_t server = NULL;
	// 创建 HTTP 服务器
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.stack_size = 8192;
	config.send_wait_timeout = 15; // 15 秒超时
	config.max_resp_headers = 16; // 增加最大响应头数量
	config.max_open_sockets = 4; // 限制最大并发连接数
	config.send_wait_timeout = 15; // 增加发送超时时间
	// 启动服务器
	if (httpd_start(&server, &config) == ESP_OK) {
		ESP_LOGI(TAG, "httpd_start  OK");

		httpd_uri_t index_uri = { .uri = "/",
					  .method = HTTP_GET,
					  .handler = index_get_handler,
					  .user_ctx = NULL };

		httpd_uri_t upload_uri = { .uri = "/upload",
					   .method = HTTP_POST,
					   .handler = upload_post_handler,
					   .user_ctx = NULL };

		httpd_uri_t favicon_uri = { .uri = "/favicon.ico",
					    .method = HTTP_GET,
					    .handler = favicon_get_handler,
					    .user_ctx = NULL };

		httpd_uri_t lang_zh_uri = {
			.uri = "/lang_zh.json",
			.method = HTTP_GET,
			.handler = lang_handler,
			.user_ctx = (void *)"/spiffs/lang_zh.json"
		};

		httpd_uri_t lang_en_uri = {
			.uri = "/lang_en.json",
			.method = HTTP_GET,
			.handler = lang_handler,
			.user_ctx = (void *)"/spiffs/lang_en.json"
		};

		httpd_uri_t lang_kr_uri = {
			.uri = "/lang_kr.json",
			.method = HTTP_GET,
			.handler = lang_handler,
			.user_ctx = (void *)"/spiffs/lang_kr.json"
		};

		httpd_uri_t device_info = { .uri = "/device_info",
					    .method = HTTP_GET,
					    .handler = device_info_handler,
					    .user_ctx = NULL };

		httpd_register_uri_handler(server, &index_uri);
		httpd_register_uri_handler(server, &upload_uri);
		httpd_register_uri_handler(server, &favicon_uri);

		httpd_register_uri_handler(server, &lang_zh_uri);
		httpd_register_uri_handler(server, &lang_en_uri);
		httpd_register_uri_handler(server, &lang_kr_uri);
		httpd_register_uri_handler(server, &device_info);
	}
}