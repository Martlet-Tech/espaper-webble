/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/****************************************************************************
 * This is a demo for bluetooth config wifi connection to ap. You can config
 *ESP32 to connect a softap or config ESP32 as a softap to be connected by other
 *device. APP can be downloaded from github android source code:
 *https://github.com/EspressifApp/EspBlufi iOS source code:
 *https://github.com/EspressifApp/EspBlufiForiOS
 ****************************************************************************/
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

// 自定义头文件，确保它们不是重复的
#include "ap.h"
#include "app.h"
#include "http_server.h"
#include "file.h"
#include "pindefine.h"
#include "EL133UF1.h"

static const char *TAG = "main";

void app_main(void)
{
	esp_err_t ret;
	heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

	// Initialize NVS
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
	    ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	app_gpio_initial();

	gpio_set_level(PIN_SW3, 1);
	vTaskDelay(20 / portTICK_PERIOD_MS);

	gpio_set_level(PIN_SW46, 1);
	vTaskDelay(200 / portTICK_PERIOD_MS);

	heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

	// 挂载 SPIFFS
	init_spiffs();

	sdcard_mount();

	ret = sdcard_test();
	if (ret != ESP_OK) {
		// TODO show sdcard error
	}

	wifi_init_softap();

	heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

	// 启动 HTTP 服务器和其他初始化
	vTaskDelay(500 / portTICK_PERIOD_MS);
	start_http_server();

	//app_start();
	EL133UF1_Init();

	ESP_LOGI(TAG, "infini loop");
	while (1) {
		vTaskDelay(200 / portTICK_PERIOD_MS);
	}
}
