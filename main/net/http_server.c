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

#define BUFFER_SIZE 4096
#define MAX_FILE_SIZE (4 * 1024 * 1024) // 假设文件大小最大为 2MB

extern YEPD *gyepd;

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

// 修改后的parse_multipart_data核心逻辑：
char *find_boundary(char *start, char *end, const char *boundary_str,
		    size_t boundary_len)
{
	for (char *ptr = start; ptr < end - boundary_len; ptr += 64) {
		if (memcmp(ptr, boundary_str, boundary_len) == 0) {
			return ptr;
		}
	}
	return NULL;
}

// 在HTTP请求处理函数中添加以下代码
char *parse_multipart_data(char *data, size_t data_len, const char *boundary,
			   const char *target_field, size_t *start_offset,
			   size_t *length)
{
	const size_t boundary_len = strlen(boundary);
	char *search_start = data;
	char *data_end = data + data_len;

	// 必须的边界格式校验
	if (boundary_len < 2 || boundary[0] != '-' || boundary[1] != '-') {
		ESP_LOGE(TAG, "Invalid boundary format");
		return NULL;
	}

	while (search_start < data_end) {
		// 查找boundary起始位置
		char *boundary_pos = NULL;
		for (char *p = search_start; p <= data_end - boundary_len;
		     p++) {
			if (memcmp(p, boundary, boundary_len) == 0) {
				boundary_pos = p;
				break;
			}
		}
		if (!boundary_pos)
			break;

		// 定位headers结束位置
		char *headers_end =
			strstr(boundary_pos + boundary_len, "\r\n\r\n");
		if (!headers_end) {
			headers_end =
				strstr(boundary_pos + boundary_len, "\n\n");
			if (!headers_end)
				break;
			headers_end += 2;
		} else {
			headers_end += 4;
		}

		// 解析字段名
		char *name_start = strstr(boundary_pos, "name=\"");
		if (!name_start) {
			search_start = boundary_pos + boundary_len;
			continue;
		}
		name_start += 6;
		char *name_end = strchr(name_start, '"');
		if (!name_end || name_end >= headers_end) {
			search_start = boundary_pos + boundary_len;
			continue;
		}

		// 匹配目标字段
		size_t name_len = name_end - name_start;
		char field_name[64] = { 0 };
		memcpy(field_name, name_start, name_len > 63 ? 63 : name_len);
		if (strcmp(field_name, target_field) != 0) {
			search_start = boundary_pos + boundary_len;
			continue;
		}

		// 定位数据区域
		char *data_start = headers_end;
		char *next_boundary = NULL;

		// 精确查找下一个boundary
		for (char *p = data_start; p <= data_end - boundary_len; p++) {
			if (memcmp(p, boundary, boundary_len) == 0) {
				next_boundary = p;
				break;
			}
		}

		// 计算数据结束位置
		char *data_end_pos = next_boundary ? next_boundary : data_end;

		// 去除尾部换行符（最多回退2字节）
		while (data_end_pos > data_start) {
			if (data_end_pos[-1] == '\n')
				data_end_pos--;
			if (data_end_pos > data_start &&
			    data_end_pos[-1] == '\r')
				data_end_pos--;
			else
				break;
		}

		// 返回结果
		*start_offset = data_start - data;
		*length = data_end_pos - data_start;
		return data_start;
	}

	return NULL;
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
	} else {
		ESP_LOGI(TAG, "File upload success, get %d Bytes", offset);
	}

	sdcard_save_buff((uint8_t *)(psram_data), offset,
			 SDCARD_MOUNT_POINT "/request.bin");

	// 在接收完数据后添加解析代码
	size_t index_offset = 0;
	size_t index_length = 0;
	char *boundary = NULL;

	// 从Content-Type头提取boundary
	char content_type[128] = { 0 };
	if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
					sizeof(content_type)) == ESP_OK) {
		char *b_start = strstr(content_type, "boundary=");
		if (b_start) {
			b_start += 9;
			char *b_end =
				strpbrk(b_start, "\r\n;"); // 兼容多种结束符
			if (!b_end)
				b_end = content_type + strlen(content_type);

			// 生成带"--"前缀的boundary
			size_t boundary_len = b_end - b_start;
			boundary = malloc(boundary_len + 3);
			snprintf(boundary, boundary_len + 3, "--%.*s",
				 boundary_len, b_start);
			ESP_LOGI(TAG, "Computed boundary: |%s|", boundary);
		}
	}

	if (boundary) {
		// 解析各个字段
		size_t field_offset, field_len;

		// 解析调色板
		if (parse_multipart_data((char *)psram_data, offset, boundary,
					 "palette", &field_offset,
					 &field_len)) {
			char *palette_str = malloc(field_len + 1);
			memcpy(palette_str, psram_data + field_offset,
			       field_len);
			palette_str[field_len] = '\0';
			ESP_LOGI(TAG, "Palette: %s", palette_str);
			free(palette_str);
		} else {
			ESP_LOGI(TAG, "Palette: parse failed");
		}

		// 解析索引数据
		if (parse_multipart_data((char *)psram_data, offset, boundary,
					 "file", &index_offset,
					 &index_length)) {
			ESP_LOGI(TAG, "Index Data Offset: %d, Length: %d",
				 index_offset, index_length);

			// 示例：打印前16字节的索引数据
			uint8_t *index_data =
				(uint8_t *)(psram_data + index_offset);
			char index_sample[65] = { 0 };
			for (int i = 0; i < 16 && i < index_length; i++) {
				sprintf(index_sample + i * 3, "%02X ",
					index_data[i]);
			}
			ESP_LOGI(TAG, "Index Sample: %s", index_sample);
		} else {
			ESP_LOGI(TAG, "index data: parse failed");
		}

		// 解析其他字段（示例）
		if (parse_multipart_data((char *)psram_data, offset, boundary,
					 "width", &field_offset, &field_len)) {
			char width_str[16] = { 0 };
			memcpy(width_str, psram_data + field_offset, field_len);
			ESP_LOGI(TAG, "Width: %s", width_str);
		} else {
			ESP_LOGI(TAG, "width: parse failed");
		}
		if (parse_multipart_data((char *)psram_data, offset, boundary,
					 "height", &field_offset, &field_len)) {
			char height_str[16] = { 0 };
			memcpy(height_str, psram_data + field_offset,
			       field_len);
			ESP_LOGI(TAG, "Height: %s", height_str);
		} else {
			ESP_LOGI(TAG, "Height: parse failed");
		}

		free(boundary);
	} else {
		ESP_LOGE(TAG, "Failed to find boundary");
	}

	// Send success response in JSON format
	httpd_resp_set_type(req, "application/json");
	const char *resp_str = "{\"code\":200, \"msg\":\"Upload complete.\"}";
	httpd_resp_send(req, resp_str, strlen(resp_str));

	display_indexed_buffer(gyepd, psram_data + index_offset);

	sdcard_save_buff((uint8_t *)(psram_data), offset,
			 SDCARD_MOUNT_POINT "/request.bin");

	// 释放 PSRAM 缓存
	free(psram_data);
	strcpy(processing_stage, "decoding");

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
	char response[256];
	snprintf(
		response, sizeof(response),
		"{\"name\":\"%s\", \"width\":\"%d\", \"height\":\"%d\", \"palette\":\"%s\"}",
		gyepd->name, gyepd->width, gyepd->height, gyepd->palette);

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