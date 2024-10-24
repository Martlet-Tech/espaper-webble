#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "file.h"
#include "util.h"
#include "img_prcs.h"

// 假设使用 PSRAM 缓存，申请内存
#define CHUNK_SIZE 512

static const char *TAG = "http_server";

esp_err_t display_jpg_file(const char *fn);

/* 根路径处理函数 */
esp_err_t index_get_handler(httpd_req_t *req)
{
	/* 打开 SPIFFS 中的 index.html 文件 */
	FILE *f = fopen(SPIFFS_MOUNT_POINT "/index.html", "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "无法打开 index.html 文件");
		/* 发送404错误页面 */
		httpd_resp_send_404(req);
		return ESP_FAIL;
	}
	const size_t line_buff_lenght = 8192;
	char *line = heap_caps_malloc(line_buff_lenght, MALLOC_CAP_SPIRAM);
	memset(line, 0, line_buff_lenght);
	int line_num = 0;
	/* 逐行读取文件内容并发送到客户端 */
	while (fgets(line, line_buff_lenght, f) != NULL) {
		httpd_resp_sendstr_chunk(req, line);
		memset(line, 0, line_buff_lenght);

		if ((line_num % 100) == 0) {
			ESP_LOGI(TAG, "line: %d", line_num);
		}
		line_num++;
	}
	heap_caps_free(line);

	/* 发送完成并关闭文件 */
	fclose(f);
	httpd_resp_sendstr_chunk(req, NULL); // 发送完最后一块数据
	ESP_LOGI(TAG, "Html send finish");
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
esp_err_t upload_post_handler(httpd_req_t *req)
{
	char buf[BUFFER_SIZE];
	int received;
	size_t remaining_size = MAX_FILE_SIZE;

	ESP_LOGI(TAG, "httpd_req_t *req->method= %d", req->method);
	ESP_LOGI(TAG, "httpd_req_t *req.uri= %s", req->uri);
	ESP_LOGI(TAG, "httpd_req_t *req.content_len= %d", req->content_len);

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
	const unsigned char *delimiter = (unsigned char *)"\x0d\x0a\x0d\x0a";
	const void *pos = memmem(psram_buf->data, 1024, delimiter, 4);
	if (pos == NULL) {
		ESP_LOGE(TAG, "File received has fault");
	}
	ptrdiff_t offset_file_start = (const unsigned char *)pos -
				      (const unsigned char *)(psram_buf->data);
	offset_file_start += 4;
	ESP_LOGI(TAG, "File offset = %d", (int)offset_file_start);

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

	// Send success response in JSON format
	httpd_resp_set_type(req, "application/json");
	const char *resp_str = "{\"code\":200, \"msg\":\"Upload complete.\"}";
	httpd_resp_send(req, resp_str, strlen(resp_str));

	ESP_LOGI(TAG, "File upload successful, total size: %zu bytes",
		 psram_buf->offset);

	// 在这里可以对缓存的数据进行处理，比如转存到 SD 卡或其他操作
	// 创建并打开文件
	FILE *f = fopen(SDCARD_MOUNT_POINT "/save.bin", "wb+");
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

	// TODO: 进度条

	// 释放 PSRAM 缓存
	free_psram_buffer(psram_buf);

	display_jpg_file(SDCARD_MOUNT_POINT "/upload.jpg");

	return ESP_OK;
}

httpd_uri_t upload_uri = { .uri = "/upload",
			   .method = HTTP_POST,
			   .handler = upload_post_handler,
			   .user_ctx = NULL };

// 启动 HTTP 服务器
void start_http_server()
{
	// 创建 HTTP 服务器
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	httpd_handle_t server = NULL;

	// 启动服务器
	if (httpd_start(&server, &config) == ESP_OK) {
		ESP_LOGI(TAG, "httpd_start  OK");
		httpd_register_uri_handler(server, &index_uri);
		httpd_register_uri_handler(server, &css_uri);
		httpd_register_uri_handler(server, &upload_uri);
	}
}
