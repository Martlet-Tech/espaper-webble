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
#include "YMS16001200-1330AAX-E6.h"
#include "img_prcs.h"
#include "utils.h"
#include "comm.h"
#include "epd.h"

static const char *TAG = "main";

extern int display_debug;

extern YEPD *epd; // global epd pointer, defined in bsp.c

void app_gpio_initial(void);
static esp_err_t save_qr_info(void);

static void check_and_show_start_screen(void);

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

	app_gpio_initial();

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

		show_ram_space("main: before show_start_screen");

		//check_and_show_start_screen();
		display_jpg_file(epd, SDCARD_MOUNT_POINT "/upload.jpg");

		show_ram_space("before exit main");
	} else {
		ESP_LOGE(TAG, "sdcard test failed");
	}
}

void app_gpio_initial(void)
{
	gpio_config_t gpiocfg_out_lcd = {};
	gpiocfg_out_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_out_lcd.mode = GPIO_MODE_OUTPUT;
	gpiocfg_out_lcd.pin_bit_mask = (1ULL << PIN_SW3) | (1ULL << PIN_SW46);
	gpiocfg_out_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_out_lcd.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&gpiocfg_out_lcd);
}

static esp_err_t save_qr_info(void)
{
	char str_wifi[256] = { 0 };
	char str_web[256] = { 0 };

// save wifi info txt
#if 1
	bsp_create_wifi_qr_str(str_wifi);
	ESP_LOGI(TAG, "wifi string(%d): %s", strlen(str_wifi), str_wifi);

	write_to_sdcard(SDCARD_MOUNT_POINT "/wifiinfo.txt", str_wifi);
	ESP_LOGI(TAG, "save wifi info OK");
#endif

// save web info txt
#if 1
	// draw webside qr
	bsp_create_web_qr_str(str_web);
	ESP_LOGI(TAG, "web string(%d): %s", strlen(str_web), str_web);

	write_to_sdcard(SDCARD_MOUNT_POINT "/webinfo.txt", str_web);
	ESP_LOGI(TAG, "save web info OK");
#endif

// save wifi info qr-jpg
#if 0
	const uint32_t out_image_size = 256;
	int qr_side = 0;
	int fret = 0;
	
	uint8_t *qr_bits_buf =
		heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	uint8_t *qr_bmp_buf = heap_caps_malloc(
		out_image_size * out_image_size * 3, MALLOC_CAP_SPIRAM);
	uint8_t *outbuf = heap_caps_malloc(100 * 1024, MALLOC_CAP_SPIRAM);

	qr_side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
			    qr_bits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(0, 0, out_image_size, qr_side, qr_bits_buf, qr_bmp_buf,
		     draw_px_24bpp);

	esp_jpeg_encode_one_picture(out_image_size, out_image_size, qr_bmp_buf,
				    outbuf);

	ESP_LOGI(TAG, "save wifi qr OK");

#endif

#if 0

	qr_side =
		qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web), qrbits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(1100, 1500, 100, qr_side, qrbits_buf, fb1);

	// put text
	char text_wifi[256];
	sprintf(text_wifi, "#1: Scan left to connect Wi-Fi <S:%s P:%s>",
		wifi_config.ap.ssid, wifi_config.ap.password);
	UG_PutString(120, 1500, text_wifi);

	UG_PutString(120, 1525, "#2: Scan Right to connect to Website");

	char text_manual[256];
	sprintf(text_manual, "Web: <%s>", str_web);
	UG_PutString(120, 1550, text_manual);

	UG_PutString(120, 1575, "#3: Select an image to upload to EPD");

		free(outbuf);
	free(qr_bmp_buf);
	free(qr_bits_buf);
#endif

	//free(str_web);
	//free(str_wifi);

	return ESP_OK;
}

void check_and_show_start_screen(void)
{
	//const char *debug_file_path = SDCARD_MOUNT_POINT "/debug.txt";
	const char *upload_file_path = SDCARD_MOUNT_POINT "/upload.jpg";

	// 尝试打开 upload 文件
	FILE *uploadFile = fopen(upload_file_path, "r");
	bool hasUploadFile = (uploadFile != NULL);
	if (hasUploadFile) {
		fclose(uploadFile); // 关闭文件
	}

	// 根据文件存在情况执行相应操作
	if (display_debug || (!display_debug && !hasUploadFile)) {
		show_start_screen(epd);
	} else if (!display_debug && hasUploadFile) {
		display_jpg_file(epd, upload_file_path);
	}
}

static void save_defconfig(FILE *file, const char *file_path)
{
	// 默认配置 JSON 字符串
	const char *default_config =
		"{\n"
		"  \"mode\": \"0\",\n"
		"  \"debug\": true\n"
		"  \"module\": \"YMS16001200-1330AAX-E6\",\n"
		"  \"password\": \"00000000\",\n"
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
	epd = yepd_init_by_name(module_name);
	if (epd != NULL) {
		printf("EPD details:\n");
		printf("  Name: %s\n", epd->name);
		printf("  Width: %d\n", epd->width);
		printf("  Height: %d\n", epd->height);
	} else {
		printf("Invalid EPD index\n");
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

	cJSON_Delete(root);
}
