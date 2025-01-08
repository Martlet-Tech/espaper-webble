/**
 * @file img_prcs.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

#include "img_prcs.h"
#include "system.h"
#include "EL133UF1.h"
#include "fs.h"

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "ugui.h"
#include "qr_encode.h"
#include "esp_jpeg_dec.h"
#include "esp_task_wdt.h"

#define PALETTE_SIZE 6
const char *TAG = "IMG_PRCS";
int display_debug = 0;

void stuckiDither(uint8_t *image, uint8_t *output_index, int image_width,
		  int image_height);
void atkinsonDither(uint8_t *image, uint8_t *output_index, int image_width,
		    int image_height);

void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s);

void draw_qrcode_on_ram(uint8_t *fb1);

esp_err_t decode_jpg(uint8_t *inbuff, uint32_t insize, uint8_t *outbuff,
		     uint32_t outsize, uint16_t *w, uint16_t *h)
{
	jpeg_error_t jd_ret;
	const char *TAG = "decode jpg";

	jpeg_dec_config_t jd_config = {
		.output_type = JPEG_PIXEL_FORMAT_RGB888,
	};

	jpeg_dec_handle_t jd_handle;
	jd_ret = jpeg_dec_open(&jd_config, &jd_handle);
	if (jd_ret != JPEG_ERR_OK) {
		ESP_LOGE(TAG, "jpeg_dec_open: %d", jd_ret);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "jpeg_dec_open %d", jd_ret);

	jpeg_dec_io_t jd_io = {
		.inbuf = inbuff,
		.inbuf_len = insize,
		.outbuf = outbuff,
		.out_size = outsize,
	};
	jpeg_dec_header_info_t jd_header_info;
	jd_ret = jpeg_dec_parse_header(jd_handle, &jd_io, &jd_header_info);
	if (jd_ret != JPEG_ERR_OK) {
		ESP_LOGE(TAG, "jpeg_dec_parse_header: %d", jd_ret);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "jpeg_dec_parse_header %d, w:%d h:%d", jd_ret,
		 jd_header_info.width, jd_header_info.height);

	int jd_out_buff_len = 0;
	jd_ret = jpeg_dec_get_outbuf_len(jd_handle, &jd_out_buff_len);
	if (jd_ret != JPEG_ERR_OK) {
		ESP_LOGE(TAG, "jpeg_dec_get_outbuf_len: %d", jd_ret);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "jpeg_dec_get_outbuf_len %d, out buff len: %d", jd_ret,
		 jd_out_buff_len);

	jd_ret = jpeg_dec_process(jd_handle, &jd_io);
	if (jd_ret != JPEG_ERR_OK) {
		ESP_LOGE(TAG, "jpeg_dec_parse_header: %d", jd_ret);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "jpeg_dec_process %d", jd_ret);

	jpeg_dec_close(jd_handle);

	return ESP_OK;
}

char processing_stage[20] = "idle"; // 初始阶段;

const char *TAG_NEWJPEGDEC = "New JPEG DEC";
esp_err_t display_jpg_file(const char *filename)
{
	const char *TAG = "display_jpg_file";
	esp_err_t ret = ESP_OK;

	uint32_t DST_FRAME_SIZE = EPD_FRAME_SIZE;
	uint8_t *jpg_file_buff = NULL;
	uint8_t *rgb_buff = NULL;
	uint8_t *index_buffer = NULL;
	uint8_t *dst_image_buffer_m = NULL;
	uint8_t *dst_image_buffer_s = NULL;

	uint16_t w = EPD_WIDTH;
	uint16_t h = EPD_HEIGHT;
	uint16_t w_img = 0;
	uint16_t h_img = 0;
	uint32_t file_size;
	uint32_t rgb_buff_size;

	//ESP_LOGI(TAG, "start, file: %s", filename);
	//show_ram_space("start of display_jpg_file");

	// read jpg file to psram

	jpg_file_buff = SD_MMC_ReadFileToPsram(filename, &file_size);
	if (jpg_file_buff == NULL) {
		return ESP_FAIL;
	}
	//show_ram_space("after malloc jpg_file_buff");

	rgb_buff_size = EPD_WIDTH * EPD_HEIGHT * 3;
	rgb_buff =
		(uint8_t *)heap_caps_malloc(rgb_buff_size, MALLOC_CAP_SPIRAM);
	if (rgb_buff == NULL) {
		ESP_LOGE(TAG, "rgb_buff malloc fail");
		return ESP_FAIL;
	}
	//show_ram_space("after malloc rgb_buff");

	// decode jpg file to rgb ram

	ESP_ERROR_CHECK(decode_jpg(jpg_file_buff, file_size, rgb_buff,
				   rgb_buff_size, &w_img, &h_img));

	free(jpg_file_buff);
	show_ram_space("after free jpg_file_buff");

	index_buffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT,
						   MALLOC_CAP_SPIRAM);
	if (index_buffer == NULL) {
		ESP_LOGE(TAG, "index_buffer malloc fail");
		return ESP_FAIL;
	}
	//show_ram_space("after malloc index_buffer");

	// process dither

	//stuckiDither((uint8_t *)org_image_buffer, (uint8_t *)index_buffer, w, h);
	atkinsonDither((uint8_t *)rgb_buff, (uint8_t *)index_buffer, w, h);

	free(rgb_buff);
	show_ram_space("after free rgb_buff");

	vTaskDelay(20 / portTICK_PERIOD_MS);

	// draw qr code

	draw_qrcode_on_ram(index_buffer);

	// make epd buff
	dst_image_buffer_m =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	dst_image_buffer_s =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	palette_index_to_E6_data(index_buffer, dst_image_buffer_m,
				 dst_image_buffer_s);
	free(index_buffer);
	show_ram_space("after free  index_buffer");

	ESP_LOGI(TAG, "取模完成");

	// update epd

	EL133UF1_Init();
	EL133UF1_DisplayFrame(dst_image_buffer_m, dst_image_buffer_s);
	EL133UF1_Sleep();
	EL133UF1_Deinit();

	free(dst_image_buffer_m);
	free(dst_image_buffer_s);
	ESP_LOGI(TAG, "显示完成");
	show_ram_space("end of display_jpg_file");

	return ret;
}

// 交换两个像素，大小为 3 字节 (RGB)
void swap_pixels(unsigned char *a, unsigned char *b)
{
	unsigned char temp[3];
	memcpy(temp, a, 3);
	memcpy(a, b, 3);
	memcpy(b, temp, 3);
}

const uint8_t palette[PALETTE_SIZE][3] = {
	{ 0x00, 0x00, 0x00 }, //BLACK 0,index = 0
	{ 0xFF, 0xFF, 0xFF }, //WHITE 1,index = 1
	{ 0xFF, 0xFF, 0x00 }, //YELLOW 2,index = 2
	{ 0xFF, 0x00, 0x00 }, //RED 3,index = 3
	{ 0x00, 0x00, 0xFF }, //BLUE 4,index = 5
	{ 0x00, 0xFF, 0x00 }, //GREEN 5,index = 6
};

uint8_t plus_truncate_uchar(uint8_t a, int b)
{
	if ((a & 0xff) + b < 0) {
		return 0;
	} else if ((a & 0xff) + b > 255) {
		return (uint8_t)255;
	} else {
		return (uint8_t)(a + b);
	}
}

uint8_t FindNearestColor(uint8_t *pixel_rgb)
{
	int minDistanceSquared = 255 * 255 + 255 * 255 + 255 * 255 + 1;
	int bestIndex = 0;
	for (int i = 0; i < PALETTE_SIZE; i++) {
		int Rdiff = ((int)pixel_rgb[0] & 0xff) -
			    ((int)palette[i][0] & 0xff);
		int Gdiff = ((int)pixel_rgb[1] & 0xff) -
			    ((int)palette[i][1] & 0xff);
		int Bdiff = ((int)pixel_rgb[2] & 0xff) -
			    ((int)palette[i][2] & 0xff);
		int distanceSquared =
			Rdiff * Rdiff + Gdiff * Gdiff + Bdiff * Bdiff;
		if (distanceSquared < minDistanceSquared) {
			minDistanceSquared = distanceSquared;
			bestIndex = i;
		}
	}
	return (uint8_t)bestIndex;
}

void stuckiDither(uint8_t *image, uint8_t *output_index, int image_width,
		  int image_height)
{
	const char *TAG = "Dithering";
	ESP_LOGI(TAG, "start");
	for (int y = 0; y < image_height; y++) {
		if (y % 10 == 0) { // 可以尝试让出 CPU 的频率，比如每 10 行
			taskYIELD(); // 或者 vTaskDelay(1)
		}

		for (int x = 0; x < image_width; x++) {
			//taskYIELD(); // 或者 vTaskDelay(1) 来让出 CPU

			uint8_t *currentPixel =
				image + (y * image_width + x) * 3;
			uint8_t index = FindNearestColor(currentPixel);
			output_index[y * image_width + x] = index;

			for (int i = 0; i < 3; i++) { //RGB
				//计算误差
				int error = (currentPixel[i] & 0xff) -
					    (palette[index][i] & 0xff);
				//扩散误差
				int pixel_offset = (y * image_width + x) * 3;
				if (x + 1 < image_width) {
					image[pixel_offset + 3 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 3 +
							      i],
							(error * 8) / 42);
				}
				if (x + 2 < image_width) {
					image[pixel_offset + 6 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 6 +
							      i],
							(error * 4) / 42);
				}
				if (y + 1 < image_height) {
					if (x - 2 > 0) {
						image[pixel_offset +
						      image_width * 3 - 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 -
								      6 + i],
								(error * 2) /
									42);
					}
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 3 - 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 -
								      3 + i],
								(error * 4) /
									42);
					}
					{ // x = x
						image[pixel_offset +
						      image_width * 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      i],
								(error * 8) /
									42);
					}
					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      3 + i],
								(error * 4) /
									42);
					}
					if (x + 2 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      6 + i],
								(error * 2) /
									42);
					}
				}
				if (y + 2 < image_height) {
					if (x - 2 > 0) {
						image[pixel_offset +
						      image_width * 6 - 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 -
								      6 + i],
								(error * 1) /
									42);
					}
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 6 - 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 -
								      3 + i],
								(error * 2) /
									42);
					}
					{ // x = x
						image[pixel_offset +
						      image_width * 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      i],
								(error * 4) /
									42);
					}
					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 6 + 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      3 + i],
								(error * 2) /
									42);
					}
					if (x + 2 < image_width) {
						image[pixel_offset +
						      image_width * 6 + 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      6 + i],
								(error * 1) /
									42);
					}
				}
			}
		}
	}

	printf(".\r\n");
}

void atkinsonDither(uint8_t *image, uint8_t *output_index, int image_width,
		    int image_height)
{
	ESP_LOGI(TAG, "dither start");

	for (int y = 0; y < image_height; y++) {
		if ((y % 80) == 0) { // 可以尝试让出 CPU 的频率，比如每 10 行
			vTaskDelay(1); //taskYIELD(); // 或者 vTaskDelay(1)
		}

		for (int x = 0; x < image_width; x++) {
			uint8_t *currentPixel =
				image + (y * image_width + x) * 3;
			uint8_t index = FindNearestColor(currentPixel);
			output_index[y * image_width + x] = index;

			for (int i = 0; i < 3; i++) { //RGB
				//计算误差
				int error = (currentPixel[i] & 0xff) -
					    (palette[index][i] & 0xff);
				//扩散误差
				int pixel_offset = (y * image_width + x) * 3;
				if (x + 1 < image_width) {
					image[pixel_offset + 3 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 3 +
							      i],
							(error * 1) / 8);
				}
				if (x + 2 < image_width) {
					image[pixel_offset + 6 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 6 +
							      i],
							(error * 1) / 8);
				}
				if (y + 1 < image_height) {
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 3 - 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 -
								      3 + i],
								(error * 1) /
									8);
					}
					{ // x = x
						image[pixel_offset +
						      image_width * 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      i],
								(error * 1) /
									8);
					}
					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      3 + i],
								(error * 1) /
									8);
					}
				}
				if (y + 2 < image_height) {
					{ // x = x
						image[pixel_offset +
						      image_width * 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      i],
								(error * 1) /
									8);
					}
				}
			}
		}
	}
}

void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s)
{
	uint8_t temp = 0;
	uint8_t index;
	int pixel_count = 0;
	for (int j = 0; j < EPD_HEIGHT; j++) {
		for (int i = 0; i < EPD_WIDTH / 2; i++) {
			temp <<= 4;
			index = index_buffer[i + j * EPD_WIDTH];
			if (index >= 4)
				index++;
			temp |= index;
			pixel_count++;
			if (pixel_count >= 2) {
				pixel_count = 0;
				*dst_m = temp;
				temp = 0;
				dst_m++;
			}
		}

		for (int i = EPD_WIDTH / 2; i < EPD_WIDTH; i++) {
			temp <<= 4;
			index = index_buffer[i + j * EPD_WIDTH];
			if (index >= 4)
				index++;
			temp |= index;
			pixel_count++;
			if (pixel_count >= 2) {
				pixel_count = 0;
				*dst_s = temp;
				temp = 0;
				dst_s++;
			}
		}
	}
}

void draw_px_ug_port(int16_t x, int16_t y, uint32_t color, void *fb)
{
	if (fb) {
		((uint8_t *)fb)[y * EPD_WIDTH + x] = color;
	} else {
		ESP_LOGE("draw_px_ug_port", "fb not initial");
	}

	if (((y % 80) == 0) || ((x % 80) == 0)) {
		vPortYield();
	}
}

void draw_px_24bpp(int16_t x, int16_t y, uint32_t color, void *fb)
{
	if (fb) {
		// Calculate the memory location of the pixel
		uint8_t *pixel_addr = ((uint8_t *)fb) + (y * EPD_WIDTH + x) * 3;

		// Extract the RGB components from the 24-bit color
		uint8_t red = (color >> 16) & 0xFF;
		uint8_t green = (color >> 8) & 0xFF;
		uint8_t blue = color & 0xFF;

		// Assign the RGB components to the framebuffer
		pixel_addr[0] = red;
		pixel_addr[1] = green;
		pixel_addr[2] = blue;
	} else {
		ESP_LOGE("draw_px_ug_port_24bpp", "fb not initial");
	}

	// Yield periodically to avoid watchdog resets in long-running loops
	if (((y % 80) == 0) || ((x % 80) == 0)) {
		vPortYield();
	}
}

void draw_qr_code(uint16_t x, uint16_t y, int width_t, int side,
		  uint8_t *bitdata, void *fb, draw_px_func_t draw_px)
{
	//PCD8544_Clear();
	int i = 0;
	int j = 0;
	int a = 0;
	int l = 0;
	int n = 0;
	int scale = 1;

	memset(fb, 0xff, width_t * width_t);

	scale = width_t / side;

	for (i = 0; i < side; i++) {
		for (j = 0; j < side; j++) {
			a = j * side + i;

			if ((bitdata[a / 8] & (1 << (7 - a % 8)))) {
				for (l = 0; l < scale; l++) {
					for (n = 0; n < scale; n++) {
						draw_px(x + scale * i + l,
							y + scale * (j) + n,
							BLACK, fb);
					}
				}
			}
		}
	}
}

void draw_qrcode_on_ram(uint8_t *fb1)
{
	if (display_debug == 0) {
		return;
	}

	const size_t str_len = 128;
	UG_GUI ug;
	int qr_side = 0;

	uint8_t *qrbits_buf =
		heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	char *str_wifi = heap_caps_malloc(str_len, MALLOC_CAP_SPIRAM);
	char *str_web = heap_caps_malloc(str_len, MALLOC_CAP_SPIRAM);

	show_ram_space("draw_qrcode_on_ram after malloc 3 ram");
	//memset(str_wifi, 0, str_len);
	//memset(str_web, 0, str_len);

	ESP_LOGI(TAG, "draw_qrcode_on_ram start");

	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, 1490, 1200 - 1, 1600 - 1, WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(BLACK);
	UG_FontSelect(&FONT_12X20);

	// draw wifi qr
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

	qr_side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
			    qrbits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(20, 1500, 100, qr_side, qrbits_buf, fb1, draw_px_ug_port);

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

	qr_side =
		qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web), qrbits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(1100, 1500, 100, qr_side, qrbits_buf, fb1,
		     draw_px_ug_port);

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

	free(str_wifi);
	free(str_web);
	free(qrbits_buf);
}

void draw_note(uint8_t *fb1)
{
	char note1[] =
		"Use the phone's built-in camera to scan the QR code because most current mobile operating systems restrict third-party applications from controlling Wi-Fi settings directly.";
	char note2[] =
		"After connecting to the Wi-Fi, you may see a message saying that this network has no internet access, or prompting you to use mobile data instead. This is normal, as this project operates locally. Choose to stay on this Wi-Fi network and ignore prompts to switch to mobile data.";

	UG_GUI ug;
	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, 1490, 1200 - 1, 1600 - 1, WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(RED);
	UG_FontSelect(&FONT_24X40);

	UG_PutString(50, 100, note1);
	UG_PutString(50, 500, note2);
}

void show_start_screen(void)
{
	show_ram_space("show_start_screen begin");

	uint8_t *fb1 = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbm = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbs = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);

	show_ram_space("show_start_screen after alloc");

	for (int y = 0; y < EPD_HEIGHT; y++) {
		for (int x = 0; x < EPD_WIDTH; x++) {
			// white
			if (x < 200) {
				draw_px_ug_port(x, y, BLACK, fb1);
			}
			// black
			if ((x >= 200) && (x < 400)) {
				draw_px_ug_port(x, y, WHITE, fb1);
			}
			// red
			if ((x >= 400) && (x < 600)) {
				draw_px_ug_port(x, y, YELLOW, fb1);
			}
			// green
			if ((x >= 600) && (x < 800)) {
				draw_px_ug_port(x, y, RED, fb1);
			}
			// blue
			if ((x >= 800) && (x < 1000)) {
				draw_px_ug_port(x, y, BLUE - 1, fb1);
			}
			// yellow
			if ((x >= 1000) && (x < 1200)) {
				draw_px_ug_port(x, y, GREEN - 1, fb1);
			}
		}
	}

	//draw_note(fb1);
	draw_qrcode_on_ram(fb1);

	// fb1 -> fb2
	palette_index_to_E6_data(fb1, fbm, fbs);

	// ppd send data, update
	EL133UF1_Init();
	EL133UF1_DisplayFrame(fbm, fbs);
	EL133UF1_Sleep();
	EL133UF1_Deinit();

	free(fbs);
	free(fbm);
	free(fb1);

	show_ram_space("show_start_screen before exit");
}
