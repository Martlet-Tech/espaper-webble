#include "file.h"

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

static const char *TAG = "file";

sdmmc_card_t *card;

/* 打印文件列表 */
void list_files(const char *base_path)
{
	DIR *dir = opendir(base_path);
	if (!dir) {
		ESP_LOGE(TAG, "无法打开目录: %s", base_path);
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		ESP_LOGI(TAG, "找到文件: %s", entry->d_name);
	}
	closedir(dir);
}

void init_spiffs(void)
{
	// 配置 SPIFFS
	esp_vfs_spiffs_conf_t conf = {
		.base_path = SPIFFS_MOUNT_POINT, // 挂载路径
		.partition_label = NULL, // 默认使用 spiffs 分区
		.max_files = 5, // 最大打开文件数量
		.format_if_mount_failed = true // 如果挂载失败，则格式化
	};

	// 挂载 SPIFFS
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",
				 esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
			 esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
	}

	// 打印文件列表
	list_files("/spiffs");
}

esp_err_t sdcard_mount()
{
	esp_err_t ret;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};

	const char mount_point[] = SDCARD_MOUNT_POINT;
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	slot_config.width = 4;
	slot_config.clk = 42;
	slot_config.cmd = 41;
	slot_config.d0 = 2;
	slot_config.d1 = 1;
	slot_config.d2 = 39;
	slot_config.d3 = 40;
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config,
				      &mount_config, &card);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(
				TAG,
				"Failed to mount filesystem. "
				"If you want the card to be formatted, set format_if_mount_failed = true.");
		} else {
			ESP_LOGE(
				TAG,
				"Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.",
				esp_err_to_name(ret));
		}
		return ESP_FAIL;
	}
	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);

	return ESP_OK;
}

esp_err_t sdcard_unmount()
{
	return esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, card);
}

esp_err_t sdcard_test()
{
	const char TAG[] = "sdmmc_card_test";
	const char file_path[] = SDCARD_MOUNT_POINT "/test.txt";

	// 创建并打开文件
	FILE *f = fopen(file_path, "w+");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 写入内容到文件
	fprintf(f, "Hello ESP32!\n");
	fclose(f);

	// 打开文件进行读取
	f = fopen(file_path, "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 读取文件内容
	char line[128];
	fgets(line, sizeof(line), f);
	printf("Read from file: %s", line);
	fclose(f);

	if (unlink(file_path) != 0) {
		ESP_LOGE(TAG, "Failed to delete file: %s", file_path);
	}

	ESP_LOGI(TAG, "OK");
	return ESP_OK;
}