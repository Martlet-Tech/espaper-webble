
#include "img_prcs.h"
#include "util.h"
#include "EL133UF1.h"
#include "file.h"

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include "ugui.h"
#include "qr_encode.h"

#define PALETTE_SIZE 6

void rotate_90_counterclockwise_rgb888(unsigned char *image, int width,
				       int height);
void stuckiDither(uint8_t *image, uint8_t *output_index, int image_width,
		  int image_height);

void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s);

void draw_qrcode_on_ram(uint8_t *fb1);

void reorder_array(uint8_t *array, int width, int height);

char processing_stage[20] = "idle"; // 初始阶段;

esp_err_t display_jpg_file(const char *filename)
{
	const char *TAG = "display_jpg_file";
	esp_err_t ret;

	uint32_t DST_FRAME_SIZE = EPD_FRAME_SIZE;
	uint8_t *dst_image_buffer_m = NULL;
	uint8_t *dst_image_buffer_s = NULL;
	uint8_t *org_image_buffer = NULL;
	uint8_t *index_buffer = NULL;

	uint8_t *jpeg_buffer;
	uint16_t w = EPD_WIDTH;
	uint16_t h = EPD_HEIGHT;
	uint32_t file_size;
	uint32_t rgb_buff_size;

	ESP_LOGI(TAG, "start, file: %s", filename);
	show_ram_space("start of display_jpg_file");

	rgb_buff_size = EPD_WIDTH * EPD_HEIGHT * 3;
	org_image_buffer =
		(uint8_t *)heap_caps_malloc(rgb_buff_size, MALLOC_CAP_SPIRAM);

	jpeg_buffer = SD_MMC_ReadFileToPsram(filename, &file_size);
	if (jpeg_buffer == NULL) {
		free(org_image_buffer);
		return ESP_FAIL;
	}

	esp_jpeg_image_cfg_t jpeg_cfg = { .indata = (uint8_t *)jpeg_buffer,
					  .indata_size = file_size,
					  .outbuf = org_image_buffer,
					  .outbuf_size = rgb_buff_size,
					  .out_format =
						  JPEG_IMAGE_FORMAT_RGB888,
					  .out_scale = JPEG_IMAGE_SCALE_0,
					  .flags = {
						  .swap_color_bytes = 0,
					  } };
	esp_jpeg_image_output_t outimg;

	ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "jpg file decode failed");
		free(org_image_buffer);
		return ESP_FAIL;
	} else {
		ESP_LOGI(TAG, "jpg file w: %d, h: %d", outimg.width,
			 outimg.height);
	}
	show_ram_space("before free jpeg_buffer");
	ESP_LOGI(TAG, "free jpg file ram");
	free(jpeg_buffer);
	show_ram_space("after free jpeg_buffer");

	/*if ((outimg.width == EPD_HEIGHT) && (outimg.height == EPD_WIDTH)) {
		ESP_LOGI(TAG, "image need to rotate 90d");
		rotate_90_counterclockwise_rgb888(org_image_buffer,
						  outimg.width, outimg.height);
	}*/

	strcpy(processing_stage, "dithering");

	show_ram_space("before malloc index_buffer");
	index_buffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT,
						   MALLOC_CAP_SPIRAM);
	show_ram_space("after malloc index_buffer");

	stuckiDither((uint8_t *)org_image_buffer, (uint8_t *)index_buffer, w,
		     h);

	free(org_image_buffer);
	show_ram_space("after free org_image_buffer");

	if ((outimg.width == EPD_HEIGHT) && (outimg.height == EPD_WIDTH)) {
		ESP_LOGI(TAG, "Image need rotation");
		reorder_array(index_buffer, outimg.width, outimg.height);
	}

	draw_qrcode_on_ram(index_buffer);

	strcpy(processing_stage, "reindexing");

	dst_image_buffer_m =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	dst_image_buffer_s =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	palette_index_to_E6_data(index_buffer, dst_image_buffer_m,
				 dst_image_buffer_s);
	free(index_buffer);
	ESP_LOGI(TAG, "取模完成");

	strcpy(processing_stage, "updating");

	EL133UF1_Init();
	EL133UF1_DisplayFrame(dst_image_buffer_m, dst_image_buffer_s);
	EL133UF1_Sleep();
	EL133UF1_Deinit();
	free(dst_image_buffer_m);
	free(dst_image_buffer_s);
	ESP_LOGI(TAG, "显示完成");
	show_ram_space("end of display_jpg_file");

	strcpy(processing_stage, "completed");

	return ESP_OK;
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
		// 每行结束后让出 CPU
		if (y % 10 == 0) { // 可以尝试让出 CPU 的频率，比如每 10 行
			printf(".");
			fflush(stdout);
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
					image[pixel_offset + image_width * 3 +
					      i] =
						plus_truncate_uchar(
							image[pixel_offset +
							      image_width * 3 +
							      i],
							(error * 8) / 42);
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
					image[pixel_offset + image_width * 6 +
					      i] =
						plus_truncate_uchar(
							image[pixel_offset +
							      image_width * 6 +
							      i],
							(error * 4) / 42);
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

void reorder_array(uint8_t *array, int width, int height)
{
	uint8_t *temp =
		(uint8_t *)malloc(width * height * sizeof(uint8_t)); // 临时缓存
	memset(temp, 0, width * height * sizeof(uint8_t));

	// 遍历每一列
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			// 原始数组中 (y, x) 的元素在新数组中的位置应该是：
			// 新数组中从左下角开始遍历，列优先
			temp[x * height + (height - 1 - y)] =
				array[y * width + x];
		}
	}

	// 将重组后的数据拷贝回原数组
	memcpy(array, temp, width * height * sizeof(uint8_t));

	free(temp); // 释放临时缓存
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

void draw_qr_code(uint16_t x, uint16_t y, int width_t, int side,
		  uint8_t *bitdata, void *fb)
{
	//PCD8544_Clear();
	int i = 0;
	int j = 0;
	int a = 0;
	int l = 0;
	int n = 0;
	int OUT_FILE_PIXEL_PRESCALER = 1;

	OUT_FILE_PIXEL_PRESCALER = width_t / side;

	for (i = 0; i < side; i++) {
		for (j = 0; j < side; j++) {
			a = j * side + i;

			if ((bitdata[a / 8] & (1 << (7 - a % 8)))) {
				for (l = 0; l < OUT_FILE_PIXEL_PRESCALER; l++) {
					for (n = 0;
					     n < OUT_FILE_PIXEL_PRESCALER;
					     n++) {
						//*(pDestData + n * 3 + unWidthAdjusted * l) = PIXEL_COLOR_B;
						/*PCD8544_DrawPixel(
							OUT_FILE_PIXEL_PRESCALER *
									i +
								l,
							OUT_FILE_PIXEL_PRESCALER *
									(j) +
								n,
							PCD8544_Pixel_Set);*/
						draw_px_ug_port(
							x +
								OUT_FILE_PIXEL_PRESCALER *
									i +
								l,
							y +
								OUT_FILE_PIXEL_PRESCALER *
									(j) +
								n,
							BLACK, fb);
					}
				}
			}
		}
	}

	//PCD8544_Refresh();
}

void show_qrcode(void)
{
	const char *TAG = "show_qrcode";
	uint32_t fb_size_ms = EPD_HEIGHT * EPD_WIDTH / 4;
	uint8_t *fb1 = NULL;
	uint8_t *fbm = NULL;
	uint8_t *fbs = NULL;

	ESP_LOGI(TAG, "show_qrcode start");

	show_ram_space("show_qrcode 1");

	fb1 = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH,
					  MALLOC_CAP_SPIRAM);
	fbm = (uint8_t *)heap_caps_malloc(fb_size_ms, MALLOC_CAP_SPIRAM);
	fbs = (uint8_t *)heap_caps_malloc(fb_size_ms, MALLOC_CAP_SPIRAM);

	show_ram_space("show_qrcode 2");

	UG_GUI ug;
	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	//UG_FillScreen(WHITE);
	//UG_FillCircle(100, 100, 30, 0x0);
	UG_FillFrame(0, 1490, 1200 - 1, 1600 - 1, WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(BLACK);
	UG_FontSelect(&FONT_12X20);
	UG_PutString(300, 1500, "Step 1: Scan left to connect Wi-Fi");
	UG_PutString(300, 1530, "Step 2: Scan Right to connect to Website");
	UG_PutString(300, 1560, "Step 3: Select an image to upload to EPD");

	wifi_config_t wifi_config;
	esp_err_t ret = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
	if (ret == ESP_OK) {
		printf("AP SSID: %s\n", wifi_config.ap.ssid);
		printf("AP Password: %s\n", wifi_config.ap.password);
	} else {
		printf("Failed to get AP config: %s\n", esp_err_to_name(ret));
	}

	char *str_wifi = heap_caps_malloc(256, MALLOC_CAP_SPIRAM);
	sprintf(str_wifi, "WIFI:T:WPA;S:%s;P:%s;;", wifi_config.ap.ssid,
		wifi_config.ap.password);
	uint8_t *qrbits_wifi =
		(uint8_t *)heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	int side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
			     qrbits_wifi);
	ESP_LOGI(TAG, "qrencode side = %d", side);
	draw_qr_code(10, 1510, 100, side, qrbits_wifi, fb1);
	free(str_wifi);
	free(qrbits_wifi);

	//======================
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif =
		esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // Station模式下

	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
		printf("IP Address: " IPSTR "\n", IP2STR(&ip_info.ip));
		printf("Netmask: " IPSTR "\n", IP2STR(&ip_info.netmask));
		printf("Gateway: " IPSTR "\n", IP2STR(&ip_info.gw));
	} else {
		printf("Failed to get IP address\n");
	}

	char *str_web = heap_caps_malloc(256, MALLOC_CAP_SPIRAM);
	sprintf(str_web, IPSTR "/?width=1200&height=1600", IP2STR(&ip_info.ip));
	uint8_t *qrbits_web =
		(uint8_t *)heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	int side_web =
		qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web), qrbits_web);
	ESP_LOGI(TAG, "qrencode side = %d", side);
	draw_qr_code(1000, 1510, 100, side_web, qrbits_web, fb1);
	free(str_web);
	free(qrbits_web);

	// fb1 -> fb2
	palette_index_to_E6_data(fb1, fbm, fbs);

	// ppd send data, update
	EL133UF1_Init();
	EL133UF1_DisplayFrame(fbm, fbs);
	EL133UF1_Sleep();
	EL133UF1_Deinit();

	free(fb1);
	free(fbm);
	free(fbs);

	show_ram_space("show_qrcode");
}

void draw_qrcode_on_ram(uint8_t *fb1)
{
	const char *TAG = "draw_qrcode_on_ram";

	UG_GUI ug;
	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, 1490, 1200 - 1, 1600 - 1, WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(BLACK);
	UG_FontSelect(&FONT_12X20);

	wifi_config_t wifi_config;
	esp_err_t ret = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
	if (ret == ESP_OK) {
		printf("AP SSID: %s\n", wifi_config.ap.ssid);
		printf("AP Password: %s\n", wifi_config.ap.password);
	} else {
		printf("Failed to get AP config: %s\n", esp_err_to_name(ret));
	}

	char *str_wifi = heap_caps_malloc(128, MALLOC_CAP_SPIRAM);
	sprintf(str_wifi, "WIFI:T:WPA;S:%s;P:%s;;", wifi_config.ap.ssid,
		wifi_config.ap.password);
	uint8_t *qrbits_wifi =
		(uint8_t *)heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	int side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
			     qrbits_wifi);
	ESP_LOGI(TAG, "qrencode side = %d", side);
	draw_qr_code(10, 1510, 100, side, qrbits_wifi, fb1);

	//======================
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif =
		esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // Station模式下

	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
		printf("IP Address: " IPSTR "\n", IP2STR(&ip_info.ip));
		printf("Netmask: " IPSTR "\n", IP2STR(&ip_info.netmask));
		printf("Gateway: " IPSTR "\n", IP2STR(&ip_info.gw));
	} else {
		printf("Failed to get IP address\n");
	}

	char *str_web = heap_caps_malloc(128, MALLOC_CAP_SPIRAM);
	sprintf(str_web, "http://" IPSTR "/?width=1200&height=1600",
		IP2STR(&ip_info.ip));
	uint8_t *qrbits_web =
		(uint8_t *)heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	int side_web =
		qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web), qrbits_web);
	ESP_LOGI(TAG, "qrencode side = %d", side);
	draw_qr_code(1000, 1510, 100, side_web, qrbits_web, fb1);

	// ========================================== put text

	char *text_wifi = heap_caps_malloc(256, MALLOC_CAP_SPIRAM);
	sprintf(text_wifi, "#1: Scan left to connect Wi-Fi <S:%s P:%s>",
		wifi_config.ap.ssid, wifi_config.ap.password);
	UG_PutString(120, 1500, text_wifi);
	free(text_wifi);

	UG_PutString(120, 1525, "#2: Scan Right to connect to Website");

	char *text_manual = heap_caps_malloc(256, MALLOC_CAP_SPIRAM);
	sprintf(text_manual, "Web: <%s>", str_web);
	UG_PutString(120, 1550, text_manual);
	free(text_manual);

	UG_PutString(120, 1575, "#3: Select an image to upload to EPD");
	// ========================================== put text

	free(str_wifi);
	free(qrbits_wifi);
	free(str_web);
	free(qrbits_web);
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
	uint8_t *fb1 = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbm = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbs = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);

	//memset(fb1, WHITE, EPD_HEIGHT * EPD_WIDTH);

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

	draw_note(fb1);
	draw_qrcode_on_ram(fb1);

	// fb1 -> fb2
	palette_index_to_E6_data(fb1, fbm, fbs);
	free(fb1);

	// ppd send data, update
	EL133UF1_Init();
	EL133UF1_DisplayFrame(fbm, fbs);
	EL133UF1_Sleep();
	EL133UF1_Deinit();

	free(fbm);
	free(fbs);

	show_ram_space("show_qrcode");
}
