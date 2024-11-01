#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "fs.h"
#include "system.h"
#include "img_prcs.h"
#include "esp_timer.h"
#include "esp_event.h"

static const char *TAG = "http_server";

static char *html_cache = NULL; // PSRAM 中的缓存指针
static size_t html_cache_size = 0; // 缓存的大小
static esp_timer_handle_t cache_timer = NULL; // 定时器句柄

bool is_busy = false;

void cache_timer_callback(void *arg);
void init_cache_timer();
void reset_cache_timer();

/* 根路径处理函数 */
esp_err_t index_get_handler(httpd_req_t *req)
{
	// 如果 HTML 缓存存在，直接返回缓存内容
	/*if (html_cache != NULL) {
		ESP_LOGI(TAG, "Serving HTML from PSRAM cache.");
		httpd_resp_send(req, html_cache, html_cache_size);
		reset_cache_timer(); // 重置定时器
		return ESP_OK;
	}*/

	// 否则，读取文件并缓存
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

	//init_cache_timer(); // 初始化定时器
	//reset_cache_timer(); // 启动定时器

	free(html_cache);

	return ESP_OK;
}

httpd_uri_t index_uri = { .uri = "/", // 根路径
			  .method = HTTP_GET, // 处理 GET 请求
			  .handler = index_get_handler, // 处理函数
			  .user_ctx = NULL };

/* CSS处理函数 */
esp_err_t css_get_handler(httpd_req_t *req)
{
	/* 打开 SPIFFS 中的 index.html 文件 */
	FILE *f = fopen(SPIFFS_MOUNT_POINT "/styles.css", "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "无法打开 index.html 文件");
		/* 发送404错误页面 */
		httpd_resp_send_404(req);
		return ESP_FAIL;
	}

	char line[256];
	/* 逐行读取文件内容并发送到客户端 */
	while (fgets(line, sizeof(line), f) != NULL) {
		httpd_resp_sendstr_chunk(req, line);
	}
	/* 发送完成并关闭文件 */
	fclose(f);
	httpd_resp_sendstr_chunk(req, NULL); // 发送完最后一块数据
	return ESP_OK;
}

httpd_uri_t css_uri = { .uri = "/styles.css",
			.method = HTTP_GET,
			.handler = css_get_handler,
			.user_ctx = NULL };

#define BUFFER_SIZE 1024
#define MAX_FILE_SIZE (4 * 1024 * 1024) // 假设文件大小最大为 2MB

typedef struct {
	char *data; // 用于存储文件数据的 PSRAM 缓存
	size_t size; // 当前缓存大小
	size_t offset; // 当前写入的偏移量
} psram_buffer_t;

// 初始化 PSRAM 缓存
psram_buffer_t *init_psram_buffer(size_t size)
{
	psram_buffer_t *buffer = malloc(sizeof(psram_buffer_t));
	if (!buffer) {
		ESP_LOGE(TAG, "Failed to allocate memory for buffer structure");
		return NULL;
	}

	// 分配 PSRAM 缓存
	buffer->data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
	if (!buffer->data) {
		ESP_LOGE(TAG, "Failed to allocate PSRAM buffer");
		free(buffer);
		return NULL;
	}

	buffer->size = size;
	buffer->offset = 0;

	return buffer;
}

// 释放 PSRAM 缓存
void free_psram_buffer(psram_buffer_t *buffer)
{
	if (buffer) {
		if (buffer->data) {
			heap_caps_free(buffer->data);
		}
		free(buffer);
	}
}

// 上传文件处理程序，存储到 PSRAM
esp_err_t upload_post_handler(httpd_req_t *req);

httpd_uri_t upload_uri = { .uri = "/upload",
			   .method = HTTP_POST,
			   .handler = upload_post_handler,
			   .user_ctx = NULL };

struct async_resp_arg {
	httpd_handle_t hd;
	int fd;
};

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
	// 发送空的响应或图标文件
	httpd_resp_send(req, "", 0); // 发送空响应
	return ESP_OK;
}

// 注册favicon处理程序
httpd_uri_t favicon_uri = { .uri = "/favicon.ico",
			    .method = HTTP_GET,
			    .handler = favicon_get_handler,
			    .user_ctx = NULL };

extern char processing_stage[];

// 处理状态的 HTTP GET 处理程序
esp_err_t status_get_handler(httpd_req_t *req)
{
	ESP_LOGI("status_get_handler", "status now: %s", processing_stage);
	httpd_resp_sendstr(req, processing_stage);
	return ESP_OK;
}

httpd_uri_t status_uri = { .uri = "/status",
			   .method = HTTP_GET,
			   .handler = status_get_handler,
			   .user_ctx = NULL };

// 启动 HTTP 服务器
void start_http_server()
{
	// 创建 HTTP 服务器
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.stack_size = 8192;
	config.send_wait_timeout = 15; // 15 秒超时
	config.max_resp_headers = 16; // 增加最大响应头数量
	config.max_open_sockets = 4; // 限制最大并发连接数
	config.send_wait_timeout = 15; // 增加发送超时时间

	httpd_handle_t server = NULL;

	// 启动服务器
	if (httpd_start(&server, &config) == ESP_OK) {
		ESP_LOGI(TAG, "httpd_start  OK");
		httpd_register_uri_handler(server, &index_uri);
		httpd_register_uri_handler(server, &upload_uri);
		httpd_register_uri_handler(server, &favicon_uri);
	}
}

int find_jpeg_start(const unsigned char *data, size_t data_len)
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

esp_err_t upload_post_handler(httpd_req_t *req)
{
	if (is_busy) {
		// 服务器忙碌，返回错误信息
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
	size_t remaining_size = MAX_FILE_SIZE;

	ESP_LOGI(TAG, "httpd_req_t *req->method= %d", req->method);
	ESP_LOGI(TAG, "httpd_req_t *req.uri= %s", req->uri);
	ESP_LOGI(TAG, "httpd_req_t *req.content_len= %d", req->content_len);

	strcpy(processing_stage, "saving");

	// 初始化 PSRAM 缓存
	psram_buffer_t *psram_buf = init_psram_buffer(MAX_FILE_SIZE);
	if (!psram_buf) {
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Receiving file and storing to PSRAM...");

	// 循环接收并存储到 PSRAM
	while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
		// 检查剩余空间是否足够
		if (psram_buf->offset + received > psram_buf->size) {
			ESP_LOGE(TAG,
				 "Not enough PSRAM space to store the file");
			free_psram_buffer(psram_buf);
			return ESP_FAIL;
		}

		// 将接收到的数据拷贝到 PSRAM 中
		memcpy(psram_buf->data + psram_buf->offset, buf, received);
		psram_buf->offset += received;
		remaining_size -= received;

		printf(".");
		fflush(stdout); // 手动刷新缓冲区
	}
	printf("\r\n");

	if (received < 0) {
		ESP_LOGE(TAG, "File upload failed");
		free_psram_buffer(psram_buf);
		return ESP_FAIL;
	}

	// find file start
	/*const unsigned char *start_string = (unsigned char *)"\x0d\x0a\x0d\x0a";
	const void *pos = memmem(psram_buf->data, 1024, start_string, 4);
	if (pos == NULL) {
		ESP_LOGE(TAG, "File received has fault");
	}
	ptrdiff_t offset_file_start = (const unsigned char *)pos -
				      (const unsigned char *)(psram_buf->data);
	offset_file_start += 4;
	ESP_LOGI(TAG, "File offset = %d", (int)offset_file_start);*/
	ptrdiff_t offset_file_start =
		find_jpeg_start((const unsigned char *)(psram_buf->data), 1024);

	const void *pos = NULL;

	// find file ends
	const char *end_string = "------WebKitFormBoundary";
	size_t search_size = req->content_len > 1024 ?
				     1024 :
				     req->content_len; // Adjust search size

	pos = memmem(psram_buf->data + req->content_len - search_size,
		     search_size, end_string, strlen(end_string));
	ptrdiff_t offset_file_end = (const unsigned char *)pos -
				    (const unsigned char *)(psram_buf->data);
	offset_file_end -= 2;
	ESP_LOGI(TAG, "File offset = %d", (int)offset_file_end);

	if ((offset_file_end < offset_file_start) || (offset_file_start < 0)) {
		ESP_LOGI(TAG, "File offset file fail");
		// Send success response in JSON format
		httpd_resp_set_type(req, "application/json");
		const char *resp_str =
			"{\"code\":500, \"msg\":\"Upload data parse fail.\"}";
		httpd_resp_send(req, resp_str, strlen(resp_str));

		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "File upload successful, total size: %zu bytes",
		 psram_buf->offset);

	// Send success response in JSON format
	httpd_resp_set_type(req, "application/json");
	const char *resp_str = "{\"code\":200, \"msg\":\"Upload complete.\"}";
	httpd_resp_send(req, resp_str, strlen(resp_str));

	// 创建并打开文件
	FILE *f = fopen(SDCARD_MOUNT_POINT "/request.bin", "wb+");
	if (f == NULL) {
		ESP_LOGE("SDMMC", "Failed to open file for writing");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 写入内容到文件
	fwrite(psram_buf->data, sizeof(char), psram_buf->offset, f);
	fclose(f);

	f = fopen(SDCARD_MOUNT_POINT "/upload.jpg", "wb+");
	if (f == NULL) {
		ESP_LOGE("SDMMC", "Failed to open file for writing");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 写入内容到文件
	fwrite(psram_buf->data + offset_file_start, sizeof(char),
	       offset_file_end - offset_file_start, f);
	fclose(f);

	// 释放 PSRAM 缓存
	free_psram_buffer(psram_buf);
	strcpy(processing_stage, "decoding");

	int64_t start_time = esp_timer_get_time();
	display_jpg_file(SDCARD_MOUNT_POINT "/upload.jpg");
	int64_t end_time = esp_timer_get_time();
	int64_t time_elapsed = end_time - start_time;
	ESP_LOGI(TAG, "display_jpg_file execution time: %lld us\n",
		 time_elapsed);

	is_busy = false;

	return ESP_OK;
}

// 定时器回调函数：释放缓存
void cache_timer_callback(void *arg)
{
	if (html_cache) {
		ESP_LOGI(TAG, "Releasing HTML cache from PSRAM.");
		show_ram_space("cache_timer_callback");
		heap_caps_free(html_cache); // 释放 PSRAM 缓存
		html_cache = NULL;
		html_cache_size = 0;
	}
}

// 初始化定时器，用于释放缓存
void init_cache_timer()
{
	if (cache_timer == NULL) {
		const esp_timer_create_args_t timer_args = {
			.callback = &cache_timer_callback,
			.name = "html_cache_timer"
		};
		esp_timer_create(&timer_args, &cache_timer);
	}
}

// 启动/重置缓存定时器
void reset_cache_timer()
{
	if (cache_timer != NULL) {
		esp_timer_stop(cache_timer); // 停止当前定时器（如果已在运行）
		esp_timer_start_once(cache_timer, 120000000); // 60秒（1分钟）
	}
}
