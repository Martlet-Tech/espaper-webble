/**
 * @file main.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <errno.h>

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "qr_encode.h"
#include "cJSON.h"

// 自定义头文件，确保它们不是重复的
#include "ap.h"
#include "http_server.h"
#include "bsp.h"
#include "img_proc.h"
#include "utils.h"
#include "yepd_if.h"
#include "yepd.h"

static const char *TAG = "main";

extern int display_show_last;
extern int display_debug;
extern int show_qr;

extern YEPD *gyepd; // global epd pointer, defined in bsp.c

void bsp_gpio_initial(void);
static esp_err_t save_qr_info(void);

void process_config(const char *file_path);

void app_main(void)
{
	esp_err_t ret;

	// Initialize NVS
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
	    ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	gpio_install_isr_service(0); // 安装 GPIO 中断服务
	bsp_gpio_initial();
	printf("GPIO monitoring initialized.\n");

	gpio_set_level(PIN_SW3, 1);
	vTaskDelay(20 / portTICK_PERIOD_MS);

	gpio_set_level(PIN_SW46, 1);
	vTaskDelay(200 / portTICK_PERIOD_MS);

	init_spiffs();

	sdcard_mount();

	if (sdcard_test() == ESP_OK) {
		process_config(SDCARD_MOUNT_POINT "/config.json");

		start_wifi_soft_ap();

		start_http_server();

		save_qr_info();

		vTaskDelay(100 / portTICK_PERIOD_MS);

		int max_jpg_number = scan_and_sort_images();
		ESP_LOGI(TAG, "max_jpg_number: %d", max_jpg_number);

		if (max_jpg_number < 1) {
			ESP_LOGE(TAG, "jpg_list is NULL");
			if (display_show_last) {
				ESP_LOGI(TAG, "display_show_last is true");
				if (is_file_exist(SDCARD_MOUNT_POINT
						  "/request.bin")) {
					ESP_LOGI(TAG, "request.bin  exist");
					display_last_data(gyepd);
				} else {
					ESP_LOGI(TAG, "request.bin not exist");
					display_palette(gyepd);
				}
			} else {
				ESP_LOGI(TAG, "display_show_last is false");
				display_palette(gyepd);
			}
		} else {
			int biggest_file_num =
				get_file_num_from_index(max_jpg_number - 1);
			set_current_image_number(biggest_file_num);

			ESP_LOGI(TAG, "jpg_list is not NULL");
			display_jpg_numble(gyepd, biggest_file_num);
		}

	} else {
		ESP_LOGE(TAG, "sdcard test failed");
	}
}

static esp_err_t save_qr_info(void)
{
	char *qr_str = NULL;

	// save wifi info txt
	if (bsp_create_wifi_qr_str(&qr_str) == ESP_OK) {
		ESP_LOGI(TAG, "wifi string(%d): %s", strlen(qr_str), qr_str);

		write_to_sdcard(SDCARD_MOUNT_POINT "/wifiinfo.txt", qr_str);
		ESP_LOGI(TAG, "save wifi info OK");
		free(qr_str); // 使用完毕后必须释放内存
	}

	// save web info txt
	if (bsp_create_web_qr_str(&qr_str) == ESP_OK) {
		ESP_LOGI(TAG, "web string(%d): %s", strlen(qr_str), qr_str);

		write_to_sdcard(SDCARD_MOUNT_POINT "/webinfo.txt", qr_str);
		ESP_LOGI(TAG, "save web info OK");
		free(qr_str); // 使用完毕后必须释放内存
	}

	return ESP_OK;
}

static void save_defconfig(FILE *file, const char *file_path)
{
	// 默认配置 JSON 字符串
	const char *default_config =
		"{\n"
		"  \"mode\": \"0\",\n"
		"  \"debug\": true,\n"
		"  \"module\": \"YMS16001200-1330AAX-E6\",\n"
		"  \"password\": \"00000000\"\n"
		"  \"show_last\": false\n"
		"}";

	// 文件不存在，创建并写入默认配置
	file = fopen(file_path, "w");
	if (!file) {
		printf("Failed to create config file: %s\n", file_path);
		return;
	}

	// 写入默认配置
	size_t written =
		fwrite(default_config, 1, strlen(default_config), file);
	fclose(file);

	if (written == strlen(default_config)) {
		printf("Default config saved to: %s\n", file_path);
	} else {
		printf("Failed to write full default config to: %s\n",
		       file_path);
	}
}
// 读取 JSON 文件并解析
void process_config(const char *file_path)
{
	FILE *file = fopen(file_path, "r");
	if (file == NULL) {
		// 要是没找到config文件, 新建一个默认配置文件
		ESP_LOGE(TAG, "Failed to open file: %s", file_path);
		save_defconfig(file, file_path);
		file = fopen(file_path, "r");
	}

	// 读取文件内容到字符串
	char buffer[512];
	size_t read_size = fread(buffer, 1, sizeof(buffer) - 1, file);
	fclose(file);

	if (read_size == 0) {
		ESP_LOGE(TAG, "Failed to read file or file is empty.");
		return;
	}

	buffer[read_size] = '\0'; // 确保字符串以 NULL 结尾

	// 解析 JSON
	cJSON *root = cJSON_Parse(buffer);
	if (root == NULL) {
		ESP_LOGE(TAG, "Error parsing JSON.");
		return;
	}

	// 获取 JSON 字段
#if 1 // 获取 module 字段值, 设定 EPD 模块
	const char *module_name =
		cJSON_GetObjectItem(root, "module")->valuestring;

	// 获取指向 epd_list 对象的指针
	gyepd = yepd_find_by_name(module_name);
	if (gyepd != NULL) {
		printf("EPD details:\n");
		printf("  Name: %s\n", gyepd->name);
		printf("  Width: %d\n", gyepd->width);
		printf("  Height: %d\n", gyepd->height);
	} else {
		printf("Invalid EPD index\n");
	}
#endif

#if 1 // 是否显示最后一次保存的数据文件
	cJSON *show_last = cJSON_GetObjectItem(root, "show_last");
	if (cJSON_IsBool(show_last)) {
		display_show_last = cJSON_IsTrue(show_last);
	} else {
		ESP_LOGE(TAG, "Error: Invalid JSON structure.");
	}

#endif

#if 1 // 获取 debug 字段值
	cJSON *debug = cJSON_GetObjectItem(root, "debug");
	if (cJSON_IsBool(debug)) {
		display_debug = cJSON_IsTrue(debug);
	} else {
		ESP_LOGE(TAG, "Error: Invalid JSON structure.");
	}
#endif

#if 1 // 获取 password 字段值
	cJSON *password = cJSON_GetObjectItem(root, "password");
	if (cJSON_IsString(password)) {
		printf("Password: %s\n", password->valuestring);
	} else {
		ESP_LOGE(TAG, "Error: Invalid JSON structure.");
	}
#endif

#if 1 // 屏幕显示QR
	cJSON *config_show_qr = cJSON_GetObjectItem(root, "show_qr");
	if (config_show_qr) {
		if (cJSON_IsBool(config_show_qr)) {
			show_qr = cJSON_IsTrue(config_show_qr);
		} else {
			ESP_LOGE(TAG, "Error: Invalid JSON structure.");
		}
	} else {
		ESP_LOGW(TAG, "show_qr not set");
	}
#endif

	cJSON_Delete(root);
}
