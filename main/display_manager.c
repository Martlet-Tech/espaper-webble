#include "display_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "yepd_if.h" // 假设你的 epd 对象在这里定义
#include <stdio.h>
#include "esp_spiffs.h"
#include <dirent.h>

static const char *TAG = "DispMgr";

YEPD *gyepd;
// 变量定义
uint8_t *g_final_buffer = NULL;
uint32_t g_buffer_size = 0;

uint8_t *display_mgr_prepare_buffer(uint32_t size)
{
	// 1. 如果之前有没释放的内存，先释放掉，防止内存泄漏
	display_mgr_release_buffer();

	ESP_LOGI(TAG, "正在申请 PSRAM: %ld 字节", size);

	// 2. 尝试从 PSRAM 申请内存
	g_final_buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

	if (g_final_buffer == NULL) {
		ESP_LOGE(TAG, "❌ PSRAM 分配失败！剩余空间不足。");
		return NULL;
	}

	g_buffer_size = size;
	return g_final_buffer;
}

void display_mgr_release_buffer(void)
{
	if (g_final_buffer != NULL) {
		free(g_final_buffer);
		g_final_buffer = NULL;
	}
	g_buffer_size = 0;
}

// 统一的刷屏任务
static void display_task(void *pvParameter)
{
	gyepd->display_index(g_final_buffer, g_buffer_size);

	vTaskDelete(NULL);
}

void display_manager_trigger_refresh(void)
{
	xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);
}

esp_err_t display_mgr_save_current_to_flash(const char *filename)
{
	if (g_final_buffer == NULL || g_buffer_size == 0) {
		ESP_LOGE("DispMgr", "缓冲区为空，无法保存");
		return ESP_FAIL;
	}

	char full_path[64];
	snprintf(full_path, sizeof(full_path), "/spiffs/data%s.bin", filename);

	ESP_LOGI("DispMgr", "正在保存到: %s", full_path);
	FILE *f = fopen(full_path, "wb");
	if (f == NULL) {
		ESP_LOGE("DispMgr", "无法打开文件!");
		return ESP_FAIL;
	}

	size_t written = fwrite(g_final_buffer, 1, g_buffer_size, f);
	fclose(f);

	if (written != g_buffer_size) {
		ESP_LOGE("DispMgr", "写入不完整!");
		return ESP_FAIL;
	}

	ESP_LOGI("DispMgr", "保存成功，大小: %d", written);
	return ESP_OK;
}