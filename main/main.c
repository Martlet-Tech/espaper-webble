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
#include "fs.h"
#include "pindefine.h"
#include "EL133UF1.h"
#include "img_prcs.h"
#include "util.h"

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

	app_gpio_initial();

	gpio_set_level(PIN_SW3, 1);
	vTaskDelay(20 / portTICK_PERIOD_MS);

	gpio_set_level(PIN_SW46, 1);
	vTaskDelay(200 / portTICK_PERIOD_MS);

	// 挂载 SPIFFS
	init_spiffs();

	sdcard_mount();

	ESP_ERROR_CHECK(sdcard_test());

	wifi_init_softap();

	// 启动 HTTP 服务器和其他初始化
	start_http_server();

	//app_start();
	EL133UF1_Init();

	vTaskDelay(1000 / portTICK_PERIOD_MS);

	show_ram_space("main: before show_start_screen");

	show_start_screen();
	//display_jpg_file("/sdcard/upload.jpg");
	show_ram_space("before exit main");
}
