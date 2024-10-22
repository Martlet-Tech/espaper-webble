#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "file.h"

// 假设使用 PSRAM 缓存，申请内存
#define CHUNK_SIZE 512

// #define FILE_UPLOAD_PATH "/spiffs/uploads/" // 上传文件的存储路径

static const char *TAG = "http_server";

/* 根路径处理函数 */
esp_err_t index_get_handler(httpd_req_t *req)
{
	/* 打开 SPIFFS 中的 index.html 文件 */
	FILE *f = fopen("/spiffs/index.html", "r");
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
			ESP_LOGI(TAG, "line: %d", line_num++);
		}
	}
	heap_caps_free(line);

	/* 发送完成并关闭文件 */
	fclose(f);
	httpd_resp_sendstr_chunk(req, NULL); // 发送完最后一块数据
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
	FILE *f = fopen("/spiffs/styles.css", "r");
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

	ESP_LOGI(TAG, "Free heap in SPIRAM: %d bytes",
		 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	ESP_LOGI(TAG, "Largest block of free heap in SPIRAM: %d bytes",
		 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

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

	if (received < 0) {
		ESP_LOGE(TAG, "File upload failed");
		free_psram_buffer(psram_buf);
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "File upload successful, total size: %zu bytes",
		 psram_buf->offset);

	// 在这里可以对缓存的数据进行处理，比如转存到 SD 卡或其他操作
	// 创建并打开文件
	FILE *f = fopen(MOUNT_POINT "/save.bin", "wb+");
	if (f == NULL) {
		ESP_LOGE("SDMMC", "Failed to open file for writing");
		esp_vfs_fat_sdmmc_unmount();
		return ESP_FAIL;
	}

	unsigned char b[10];
	for (int i = 0; i < 10; i++) {
		b[i] = 0xff;
	}

	// 写入内容到文件
	fwrite(psram_buf->data, sizeof(char), psram_buf->offset, f);
	fclose(f);
	// TODO: 存储到 SD 卡或其他地方

	// 响应客户端
	httpd_resp_sendstr(req,
			   "File uploaded and stored in PSRAM successfully!");

	// 释放 PSRAM 缓存
	free_psram_buffer(psram_buf);

	return ESP_OK;
}

httpd_uri_t upload_uri = { .uri = "/upload",
			   .method = HTTP_POST,
			   .handler = upload_post_handler,
			   .user_ctx = NULL };

#if 0
esp_err_t get_html_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "Callling get_html_handler");
	FILE *f = fopen("/spiffs/index.html", "r");
	if (!f) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		httpd_resp_send_404(req);
		return ESP_FAIL;
	}

	char buf[1024];
	size_t read_len;
	while ((read_len = fread(buf, 1, sizeof(buf), f)) > 0) {
		httpd_resp_send_chunk(req, buf, read_len);
	}

	fclose(f);
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}
#endif

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
