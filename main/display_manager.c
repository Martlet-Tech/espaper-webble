#include "display_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "yepd_if.h" // 假设你的 epd 对象在这里定义
#include <stdio.h>
#include "esp_spiffs.h"
#include <dirent.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "DispMgr";

YEPD *gyepd;

// 硬件互斥锁，确保刷新过程不被中断
static SemaphoreHandle_t xHardwareMutex = NULL;
// 用户操作时间戳（用于避让逻辑）
static TickType_t g_last_user_action_tick = 0;

// 独立的双缓冲区信息
typedef struct {
	uint8_t *ptr;
	uint32_t size;
} DisplayBuffer;

static DisplayBuffer g_user_buf = { NULL, 0 };
static DisplayBuffer g_album_buf = { NULL, 0 };

// 初始化管理模块
void display_mgr_init(void)
{
	if (xHardwareMutex == NULL) {
		xHardwareMutex = xSemaphoreCreateMutex();
	}
	g_last_user_action_tick = xTaskGetTickCount();
}

// 通用的缓冲区申请函数（按需分配）
static uint8_t *allocate_buffer(DisplayBuffer *buf, uint32_t size)
{
	if (buf->ptr != NULL) {
		heap_caps_free(buf->ptr);
	}
	buf->ptr = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
	buf->size = (buf->ptr) ? size : 0;
	return buf->ptr;
}

// 暴露给 wifi_sta.c 的用户缓冲区准备函数
uint8_t *display_mgr_prepare_user_buffer(uint32_t size)
{
	return allocate_buffer(&g_user_buf, size);
}

// 核心刷新函数：所有刷屏请求最终都汇聚于此
static void perform_hardware_display(uint8_t *data, uint32_t size, bool is_user_action)
{
	if (data == NULL || size == 0)
		return;

	// 1. 等待硬件空闲（如果正在刷屏，会在这里阻塞直到上一个 20s 结束）
	ESP_LOGI(TAG, "等待硬件锁...");
	if (xSemaphoreTake(xHardwareMutex, portMAX_DELAY) == pdTRUE) {
		ESP_LOGI(TAG, ">>> 开始刷新硬件 (%s)...", is_user_action ? "用户上传" : "相册模式");

		// 执行实际的刷新（假设该函数阻塞 20s）
		gyepd->display_index(data, size);

		ESP_LOGI(TAG, "<<< 刷新硬件完成");

		if (is_user_action) {
			// 如果是用户操作，更新时间戳，强制后续轮播避让 2 分钟
			g_last_user_action_tick = xTaskGetTickCount();
		}

		xSemaphoreGive(xHardwareMutex);
	}
}

// 刷新任务包装器（由 Task 调用）
static void display_task_entry(void *pvParameter)
{
	DisplayBuffer *target = (DisplayBuffer *)pvParameter;
	bool is_user = (target == &g_user_buf);

	perform_hardware_display(target->ptr, target->size, is_user);

	vTaskDelete(NULL);
}

// 触发用户图片的刷新
void display_mgr_trigger_user_refresh(void)
{
	xTaskCreate(display_task_entry, "user_disp_task", 8192, &g_user_buf, 5, NULL);
}

esp_err_t display_mgr_save_current_to_flash(const char *filename)
{
	char full_path[64];
	snprintf(full_path, sizeof(full_path), "/spiffs/data%s.bin", filename);

	ESP_LOGI("DispMgr", "正在保存到: %s", full_path);
	FILE *f = fopen(full_path, "wb");
	if (f == NULL) {
		ESP_LOGE("DispMgr", "无法打开文件!");
		return ESP_FAIL;
	}

	size_t written = fwrite(g_user_buf.ptr, 1, g_user_buf.size, f);
	fclose(f);

	if (written != g_user_buf.size) {
		ESP_LOGE("DispMgr", "写入不完整!");
		return ESP_FAIL;
	}

	ESP_LOGI("DispMgr", "保存成功，大小: %d", written);
	return ESP_OK;
}

esp_err_t display_mgr_clear_flash_images(void)
{
	DIR *dp = opendir("/spiffs");
	if (!dp) {
		ESP_LOGE("DispMgr", "无法打开目录进行清空");
		return ESP_FAIL;
	}

	struct dirent *entry;
	int deleted_count = 0;
	while ((entry = readdir(dp)) != NULL) {
		// 只删除图片数据文件，不删除网页相关的 .html, .css, .js
		if (strstr(entry->d_name, "data") && strstr(entry->d_name, ".bin")) {
			char full_path[265];
			snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);
			if (unlink(full_path) == 0) {
				deleted_count++;
			}
		}
	}
	closedir(dp);
	ESP_LOGI("DispMgr", "清空完成，共删除 %d 个文件", deleted_count);
	return ESP_OK;
}

// ---------------- 相册轮播模式 ----------------

void album_mode_task(void *pvParameters)
{
	ESP_LOGI("Album", "相册模式启动...");
	const TickType_t pause_interval = pdMS_TO_TICKS(20 * 1000); // 2分钟避让期

	while (1) {
		// 1. 避让检查：如果距离上次用户操作不足 2 分钟，则休眠等待
		TickType_t now = xTaskGetTickCount();
		if (now - g_last_user_action_tick < pause_interval) {
			ESP_LOGD("Album", "用户近期有操作，相册模式避让中...");
			vTaskDelay(pdMS_TO_TICKS(5000));
			continue;
		}

		DIR *dp = opendir("/spiffs");
		if (!dp) {
			vTaskDelay(pdMS_TO_TICKS(10000));
			continue;
		}

		struct dirent *entry;
		while ((entry = readdir(dp)) != NULL) {
			if (strstr(entry->d_name, "data") && strstr(entry->d_name, ".bin")) {
				// 再次检查用户避让（防止遍历文件期间用户突然操作）
				if (xTaskGetTickCount() - g_last_user_action_tick < pause_interval)
					break;

				char full_path[300];
				snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);

				struct stat st;
				if (stat(full_path, &st) == 0) {
					// 申请相册专用缓冲区并读取文件
					uint8_t *buf = allocate_buffer(&g_album_buf, st.st_size);
					if (buf) {
						FILE *f = fopen(full_path, "rb");
						fread(buf, 1, st.st_size, f);
						fclose(f);

						ESP_LOGI("Album", "准备轮播: %s", entry->d_name);
						// 调用硬件刷新逻辑（会等待硬件锁）
						perform_hardware_display(g_album_buf.ptr, g_album_buf.size, false);

						vTaskDelay(pdMS_TO_TICKS(1 * 60 * 1000));
					}
				}
			}
		}
		closedir(dp);
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}