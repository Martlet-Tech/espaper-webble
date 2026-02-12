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

static const char *TAG = "DispMgr";

YEPD *gyepd;
// 变量定义
uint8_t *g_final_buffer = NULL;
uint32_t g_buffer_size = 0;

static bool g_is_displaying = false; // 全局刷屏状态锁

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
	if (g_is_displaying) {
		ESP_LOGW("DispMgr", "硬件忙！拒绝本次刷屏请求");
		vTaskDelete(NULL);
		return;
	}

	g_is_displaying = true; // 上锁

	ESP_LOGI(TAG, ">>> 开始硬件刷新...");
	gyepd->display_index(g_final_buffer, g_buffer_size);
	ESP_LOGI(TAG, "<<< 硬件刷新完成");

	g_is_displaying = false; // 解锁
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

// 简单的轮播任务
void album_mode_task(void *pvParameters)
{
	ESP_LOGI("Album", "相册模式启动...");

	while (1) {
		// ... 遍历文件逻辑 ...
		DIR *dp = opendir("/spiffs");
		if (!dp) {
			ESP_LOGE("Album", "无法打开目录");
			vTaskDelay(pdMS_TO_TICKS(5000));
			continue;
		}

		struct dirent *entry;
		bool found_any = false;

		while ((entry = readdir(dp)) != NULL) {
			// 匹配你的文件名格式 data2026...bin
			if (strstr(entry->d_name, "data") && strstr(entry->d_name, ".bin")) {
				found_any = true;
				char full_path[256 + 16];
				snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);

				ESP_LOGI("Album", "正在轮播图片: %s", full_path);

				// 1. 检查硬件是否正在被 WiFi/BLE 占用
				if (g_is_displaying) {
					ESP_LOGI("Album", "硬件忙，避让中...");
					vTaskDelay(pdMS_TO_TICKS(5000)); // 歇 5 秒再看
					continue;
				}

				// 加载并显示
				struct stat st;
				if (stat(full_path, &st) == 0) {
					uint8_t *buf = display_mgr_prepare_buffer(st.st_size);
					if (buf) {
						FILE *f = fopen(full_path, "rb");
						fread(buf, 1, st.st_size, f);
						fclose(f);
						g_buffer_size = st.st_size;

						// 3. 再次确认锁（防止读取文件期间被 WiFi 抢占）
						if (!g_is_displaying) {
							g_buffer_size = st.st_size;
							display_manager_trigger_refresh();
						}

						// 💡 重点：电子纸刷新慢，且为了省电/保护屏幕，建议轮播间隔长一点
						// 比如 10 分钟换一张图
						vTaskDelay(pdMS_TO_TICKS(1 * 60 * 1000));
					}
				}
			}
		}
		closedir(dp);

		if (!found_any) {
			ESP_LOGW("Album", "未找到任何图片，10秒后重试");
			vTaskDelay(pdMS_TO_TICKS(10000));
		}
	}
}