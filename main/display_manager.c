#include "display_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "yepd_if.h" // 假设你的 epd 对象在这里定义

static const char *TAG = "DispMgr";

YEPD *gyepd;
// 变量定义
uint8_t *g_final_buffer = NULL;
size_t g_buffer_size = 0;

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