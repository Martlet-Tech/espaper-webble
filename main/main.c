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

// 自定义头文件，确保它们不是重复的
#include "ap.h"
#include "http_server.h"
#include "fs.h"
#include "pindefine.h"
#include "EL133UF1.h"
#include "img_prcs.h"
#include "system.h"
#include <esp_jpeg_enc.h>
#include "comm.h"

static const char *TAG = "main";

extern int display_debug;

spi_device_handle_t spi;

void app_gpio_initial(void);
static esp_err_t save_qr_info(void);
jpeg_error_t esp_jpeg_encode_one_picture(uint32_t w, uint32_t h, uint8_t *inbuf,
					 uint8_t *outbuf);
static void check_and_show_start_screen(void);

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

	save_qr_info();

	//app_start();
	EL133UF1_Init();

	vTaskDelay(1000 / portTICK_PERIOD_MS);

	show_ram_space("main: before show_start_screen");

	//display_jpg_file("/sdcard/upload.jpg");
	//show_start_screen();
	check_and_show_start_screen();

	show_ram_space("before exit main");
}

void app_gpio_initial(void)
{
	esp_err_t ret;

	spi_bus_config_t bus_config = {
		.mosi_io_num = SPI_Data0_MOSI,
		.miso_io_num = SPI_Data1_MISO,
		.sclk_io_num = SPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = CHUNK_SIZE,
	};
	ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	spi_device_interface_config_t dev_config_0 = {
		.clock_speed_hz = 16000000,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 7,
		.command_bits = 0,
		.address_bits = 0,
		.dummy_bits = 0,
		//.duty_cycle_pos = 128,
		//.flags = SPI_DEVICE_HALFDUPLEX, // 使用半双工模式
	};

	// TEST_ESP_OK(spi_bus_initialize(TEST_SPI_HOST, &buscfg, dma ? SPI_DMA_CH_AUTO : 0));
	ret = spi_bus_add_device(SPI2_HOST, &dev_config_0, &spi);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	gpio_config_t gpiocfg_out_lcd = {};
	gpiocfg_out_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_out_lcd.mode = GPIO_MODE_OUTPUT;
	gpiocfg_out_lcd.pin_bit_mask = (1ULL << EPD_RST) | (1ULL << PIN_CS_M) |
				       (1ULL << PIN_CS_S) | (1ULL << PIN_SW3) |
				       (1ULL << PIN_SW46);
	gpiocfg_out_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_out_lcd.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&gpiocfg_out_lcd);

	gpio_config_t gpiocfg_in_lcd = {};
	gpiocfg_in_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_in_lcd.mode = GPIO_MODE_INPUT;
	gpiocfg_in_lcd.pin_bit_mask = (1ULL << EPD_BUSY);
	gpiocfg_in_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_in_lcd.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&gpiocfg_in_lcd);
}

static esp_err_t save_qr_info(void)
{
	//const size_t str_len = 128;
	const uint32_t out_image_size = 256;
	int qr_side = 0;
	int fret = 0;

	char str_wifi[256];
	char str_web[256];

	show_ram_space("draw_qrcode_on_ram after malloc 3 ram");

	/*UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, 1490, 1200 - 1, 1600 - 1, WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(BLACK);
	UG_FontSelect(&FONT_12X20);*/

// save wifi info txt
#if 1
	wifi_config_t wifi_config;
	esp_err_t ret = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "AP SSID: %s,  Password: %s", wifi_config.ap.ssid,
			 wifi_config.ap.password);
	} else {
		ESP_LOGE(TAG, "Failed to get AP config: %s\n",
			 esp_err_to_name(ret));
	}

	sprintf(str_wifi, "WIFI:T:WPA;S:%s;P:%s;;", wifi_config.ap.ssid,
		wifi_config.ap.password);

	ESP_LOGI(TAG, "wifi string(%d): %s", strlen(str_wifi), str_wifi);

	write_to_sdcard(SDCARD_MOUNT_POINT "/wifiinfo.txt", str_wifi);
	// 创建并打开文件
	//FILE *f = fopen(SDCARD_MOUNT_POINT "/wifiinfo.txt", "w+");
	//if (f == NULL) {
	//	ESP_LOGE(TAG, "Failed to open file for writing");
	//	sdcard_unmount();
	//	return ESP_FAIL;
	//}
	//
	//// 写入内容到文件
	//fret = fprintf(f, "%s", str_wifi);
	//if (fret < 0) {
	//	// fprintf 失败
	//	ESP_LOGE(TAG, "Failed to write to file");
	//	fclose(f);
	//	sdcard_unmount();
	//	return ESP_FAIL;
	//} else if (fret != strlen(str_wifi)) {
	//	// 写入的字符数与字符串长度不符，可能存在部分写入失败的情况
	//	ESP_LOGW(TAG, "Partial write to file: expected %zu, wrote %d",
	//		 strlen(str_wifi), fret);
	//}
	//fclose(f);

	ESP_LOGI(TAG, "save wifi info OK");

#endif

// save wifi info qr-jpg
#if 0
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

// save web info txt
#if 1
	// draw webside qr
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif =
		esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // Station模式下

	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
		ESP_LOGI(TAG, "IP Address: " IPSTR "\n", IP2STR(&ip_info.ip));
	} else {
		ESP_LOGE(TAG, "Failed to get IP address\n");
	}
	sprintf(str_web, "http://" IPSTR "/?width=1200&height=1600",
		IP2STR(&ip_info.ip));
	ESP_LOGI(TAG, "web string(%d): %s", strlen(str_web), str_web);

	write_to_sdcard(SDCARD_MOUNT_POINT "/webinfo.txt", str_web);

	ESP_LOGI(TAG, "save web info OK");
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

jpeg_error_t esp_jpeg_encode_one_picture(uint32_t w, uint32_t h, uint8_t *inbuf,
					 uint8_t *outbuf)
{
	// configure encoder
	jpeg_enc_config_t jpeg_enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
	jpeg_enc_cfg.width = w;
	jpeg_enc_cfg.height = h;
	jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
	jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
	jpeg_enc_cfg.quality = 60;
	jpeg_enc_cfg.rotate = JPEG_ROTATE_0D;
	jpeg_enc_cfg.task_enable = false;
	jpeg_enc_cfg.hfm_task_priority = 13;
	jpeg_enc_cfg.hfm_task_core = 1;

	jpeg_error_t ret = JPEG_ERR_OK;
	//uint8_t *inbuf = test_rgb888_data;
	int image_size = jpeg_enc_cfg.width * jpeg_enc_cfg.height * 3;
	//uint8_t *outbuf = NULL;
	//int outbuf_size = 1024;
	int out_len = 0;
	jpeg_enc_handle_t jpeg_enc = NULL;
	FILE *out = NULL;

	// open
	ret = jpeg_enc_open(&jpeg_enc_cfg, &jpeg_enc);
	if (ret != JPEG_ERR_OK) {
		return ret;
	}

	// allocate output buffer to fill encoded image stream
	// outbuf = (uint8_t *)calloc(1, outbuf_size);
	//outbuf = heap_caps_malloc(100 * 1024, MALLOC_CAP_SPIRAM);
	//if (outbuf == NULL) {
	//	ret = JPEG_ERR_NO_MEM;
	//	goto jpeg_example_exit;
	//}

	// process
	ret = jpeg_enc_process(jpeg_enc, inbuf, image_size, outbuf, 100 * 1024,
			       &out_len);
	if (ret != JPEG_ERR_OK) {
		goto jpeg_example_exit;
	}

	out = fopen("/sdcard/qr_wifi.jpg", "wb+");
	if (out == NULL) {
		goto jpeg_example_exit;
	}
	fwrite(outbuf, 1, out_len, out);
	fclose(out);

jpeg_example_exit:
	// close
	jpeg_enc_close(jpeg_enc);
	//if (outbuf) {
	//	free(outbuf);
	//}
	return ret;
}

static void check_and_show_start_screen(void)
{
	const char *debug_file_path = SDCARD_MOUNT_POINT "/debug.txt";
	const char *upload_file_path = SDCARD_MOUNT_POINT "/upload.jpg";

	// 尝试打开 debug 文件
	FILE *debugFile = fopen(debug_file_path, "r");
	bool hasDebugFile = (debugFile != NULL);
	if (hasDebugFile) {
		display_debug = 1;

		fclose(debugFile); // 关闭文件
	}

	// 尝试打开 upload 文件
	FILE *uploadFile = fopen(upload_file_path, "r");
	bool hasUploadFile = (uploadFile != NULL);
	if (hasUploadFile) {
		fclose(uploadFile); // 关闭文件
	}

	// 根据文件存在情况执行相应操作
	if (hasDebugFile || (!hasDebugFile && !hasUploadFile)) {
		show_start_screen();
	} else if (!hasDebugFile && hasUploadFile) {
		display_jpg_file(upload_file_path);
	}
}
