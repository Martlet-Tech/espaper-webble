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
#include "utils.h"
#include "YMS16001200-1330AAX-E6.h"
#include "bsp.h"
#include <string.h>

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "ugui.h"
#include "qr_encode.h"
#include "esp_jpeg_dec.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include <esp_netif.h>
#include <esp_netif_types.h>
#include "esp_wifi.h"

const char *TAG = "IMG_PRCS";
int display_debug = 0;
char processing_stage[20] = "idle"; // 初始阶段;

void draw_qrcode_on_ram(YEPD *epd, uint8_t *fb1);

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

uint8_t **parse_palette(const char *palette_str, size_t *color_count)
{
	if (!palette_str || !color_count) {
		return NULL; // 参数错误
	}

	// 初始化颜色计数
	*color_count = 0;

	// 预估调色板中的分号数量，计算大致的颜色数量
	size_t estimated_colors = 1;
	for (const char *p = palette_str; *p != '\0'; p++) {
		if (*p == ';') {
			estimated_colors++;
		}
	}

	// 动态分配二维数组指针
	uint8_t **palette =
		(uint8_t **)malloc(estimated_colors * sizeof(uint8_t *));
	if (!palette) {
		return NULL; // 分配失败
	}

	const char *current = palette_str;
	char *end_ptr;

	// 循环解析调色板字符串
	while (*current != '\0') {
		if (*color_count >= estimated_colors) {
			// 空间不足，扩容
			estimated_colors *= 2;
			uint8_t **temp = (uint8_t **)realloc(
				palette, estimated_colors * sizeof(uint8_t *));
			if (!temp) {
				for (size_t i = 0; i < *color_count; i++) {
					free(palette[i]);
				}
				free(palette);
				return NULL; // 扩容失败
			}
			palette = temp;
		}

		// 为每个颜色分配 3 个字节的空间
		palette[*color_count] = (uint8_t *)malloc(3 * sizeof(uint8_t));
		if (!palette[*color_count]) {
			for (size_t i = 0; i < *color_count; i++) {
				free(palette[i]);
			}
			free(palette);
			return NULL; // 分配失败
		}

		// 解析 R
		int r = strtol(current, &end_ptr, 10);
		if (*end_ptr != ',')
			break; // 格式错误
		current = end_ptr + 1;

		// 解析 G
		int g = strtol(current, &end_ptr, 10);
		if (*end_ptr != ',')
			break; // 格式错误
		current = end_ptr + 1;

		// 解析 B
		int b = strtol(current, &end_ptr, 10);
		if (*end_ptr != ';' && *end_ptr != '\0')
			break; // 格式错误
		current = (*end_ptr == ';') ? end_ptr + 1 : end_ptr;

		// 保存颜色
		palette[*color_count][0] = (uint8_t)r;
		palette[*color_count][1] = (uint8_t)g;
		palette[*color_count][2] = (uint8_t)b;

		(*color_count)++;
	}

	// 调整内存到最终大小
	uint8_t **final_palette = (uint8_t **)realloc(
		palette, (*color_count) * sizeof(uint8_t *));
	if (!final_palette && *color_count > 0) {
		for (size_t i = 0; i < *color_count; i++) {
			free(palette[i]);
		}
		free(palette);
		return NULL; // 调整失败
	}

	return final_palette;
}

uint8_t bounded_add_u8(uint8_t a, int b)
{
	if ((a & 0xff) + b < 0) {
		return 0;
	} else if ((a & 0xff) + b > 255) {
		return (uint8_t)255;
	} else {
		return (uint8_t)(a + b);
	}
}

uint8_t find_nearest_color(uint8_t *pixel_rgb, uint8_t **palette,
			   size_t palette_size)
{
	int minDistanceSquared = 255 * 255 + 255 * 255 + 255 * 255 + 1;
	int bestIndex = 0;
	for (size_t i = 0; i < palette_size; i++) {
		int Rdiff = ((int)pixel_rgb[0]) - ((int)palette[i][0]);
		int Gdiff = ((int)pixel_rgb[1]) - ((int)palette[i][1]);
		int Bdiff = ((int)pixel_rgb[2]) - ((int)palette[i][2]);
		int distanceSquared =
			Rdiff * Rdiff + Gdiff * Gdiff + Bdiff * Bdiff;

		if (distanceSquared < minDistanceSquared) {
			minDistanceSquared = distanceSquared;
			bestIndex = i;
		}
	}
	return (uint8_t)bestIndex;
}

void atkinson_dither(uint8_t *image, uint8_t *output_index, int image_width,
		     int image_height, uint8_t **palette, size_t palette_size)
{
	ESP_LOGI(TAG, "dither start");

	for (int y = 0; y < image_height; y++) {
		if ((y % 200) == 0) {
			vTaskDelay(1); // 可以尝试让出 CPU 的频率
		}

		for (int x = 0; x < image_width; x++) {
			uint8_t *currentPixel =
				image + (y * image_width + x) * 3;
			uint8_t index = find_nearest_color(
				currentPixel, palette, palette_size);
			output_index[y * image_width + x] = index;

			for (int i = 0; i < 3; i++) { // RGB
				// 计算误差
				int error = (currentPixel[i] & 0xff) -
					    (palette[index][i] & 0xff);
				// 扩散误差
				int pixel_offset = (y * image_width + x) * 3;
				if (x + 1 < image_width) {
					image[pixel_offset + 3 + i] =
						bounded_add_u8(
							image[pixel_offset + 3 +
							      i],
							(error >> 3));
				}
				if (x + 2 < image_width) {
					image[pixel_offset + 6 + i] =
						bounded_add_u8(
							image[pixel_offset + 6 +
							      i],
							(error >> 3));
				}
				if (y + 1 < image_height) {
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 3 - 3 + i] =
							bounded_add_u8(
								image[pixel_offset +
								      image_width *
									      3 -
								      3 + i],
								(error >> 3));
					}
					image[pixel_offset + image_width * 3 +
					      i] =
						bounded_add_u8(
							image[pixel_offset +
							      image_width * 3 +
							      i],
							(error >> 3));

					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 3 + i] =
							bounded_add_u8(
								image[pixel_offset +
								      image_width *
									      3 +
								      3 + i],
								(error >> 3));
					}
				}
				if (y + 2 < image_height) {
					image[pixel_offset + image_width * 6 +
					      i] =
						bounded_add_u8(
							image[pixel_offset +
							      image_width * 6 +
							      i],
							(error >> 3));
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

esp_err_t display_jpg_file(YEPD *epd, const char *filename)
{
	const char *TAG = "display_jpg_file";
	esp_err_t ret = ESP_OK;

	uint32_t file_size = 0;
	uint8_t *jpg_file_buff = NULL;
	uint8_t *rgb_buff = NULL;
	uint32_t rgb_buff_size = epd->width * epd->height * 3;

	uint16_t w_img = 0;
	uint16_t h_img = 0;

	// read jpg file to psram
	jpg_file_buff = SD_MMC_ReadFileToPsram(filename, &file_size);
	if ((jpg_file_buff == NULL) || (file_size == 0)) {
		ESP_LOGE(TAG, "read jpg file fail");
		return ESP_FAIL;
	}

	// alloc rgb_buff
	rgb_buff = heap_caps_malloc(rgb_buff_size, MALLOC_CAP_SPIRAM);
	if (rgb_buff == NULL) {
		ESP_LOGE(TAG, "rgb_buff malloc fail");
		return ESP_FAIL;
	}
	show_ram_space("after malloc rgb_buff");

	// decode jpg file to rgb_buff
	ESP_ERROR_CHECK(decode_jpg(jpg_file_buff, file_size, rgb_buff,
				   rgb_buff_size, &w_img, &h_img));

	free(jpg_file_buff);
	show_ram_space("after free jpg_file_buff");

	// 解析调色板
	size_t color_count = 0;
	uint8_t **palette = parse_palette(epd->palette, &color_count);
	if (!palette) {
		printf("Failed to parse palette.\n");
		return 1;
	}
	printf("Parsed %zu colors:\n", color_count);
	for (size_t i = 0; i < color_count; i++) {
		printf("Color %zu: R=%d, G=%d, B=%d\n", i, palette[i][0],
		       palette[i][1], palette[i][2]);
	}

	// 像素对调色板索引缓存
	uint8_t *index_buffer =
		heap_caps_malloc(epd->width * epd->height, MALLOC_CAP_SPIRAM);
	if (index_buffer == NULL) {
		ESP_LOGE(TAG, "index_buffer malloc fail");
		return ESP_FAIL;
	}
	show_ram_space("after malloc index_buffer");

	// process dither
	int64_t start_time = esp_timer_get_time();
	atkinson_dither(rgb_buff, index_buffer, epd->width, epd->height,
			palette, color_count);
	int64_t end_time = esp_timer_get_time();
	int64_t time_elapsed = end_time - start_time;
	ESP_LOGI(TAG, "dither execution time: %lld us\n", time_elapsed);

	show_ram_space("after dither, before free rgb_buff");

	free(rgb_buff);
	show_ram_space("after free rgb_buff");

	epd->init();
	epd->fill_index(index_buffer);
	epd->update();

	free(index_buffer);
	free(palette); // 释放数组指针

	return ret;
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

void draw_qrcode_on_ram(YEPD *epd, uint8_t *fb1)
{
	if (display_debug == 0) {
		return;
	}

	UG_GUI ug;
	int qr_side = 0;

	uint8_t *qrbits_buf =
		heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);
	char str_wifi[128] = { 0 };
	char str_web[128] = { 0 };

	show_ram_space("draw_qrcode_on_ram after malloc 3 ram");

	ESP_LOGI(TAG, "draw_qrcode_on_ram start");

	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, epd->height - 110, epd->width - 1, epd->height - 1,
		     WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(BLACK);
	UG_FontSelect(&FONT_12X20);

	// draw wifi qr
	bsp_create_wifi_qr_str(str_wifi);
	ESP_LOGI(TAG, "wifi string(%d): %s", strlen(str_wifi), str_wifi);

	qr_side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
			    qrbits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(20, 1500, 100, qr_side, qrbits_buf, fb1, draw_px_ug_port);

	// draw webside qr
	bsp_create_web_qr_str(str_web);
	ESP_LOGI(TAG, "web string(%d): %s", strlen(str_web), str_web);

	qr_side =
		qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web), qrbits_buf);
	ESP_LOGI(TAG, "qrencode side = %d", qr_side);

	draw_qr_code(1100, 1500, 100, qr_side, qrbits_buf, fb1,
		     draw_px_ug_port);

	// put text
	char text_wifi[256];
	sprintf(text_wifi, "#1: Scan left to connect Wi-Fi: %s", str_wifi);
	UG_PutString(120, 1500, text_wifi);

	UG_PutString(120, 1525, "#2: Scan Right to connect to Website");

	char text_manual[256];
	sprintf(text_manual, "Web: <%s>", str_web);
	UG_PutString(120, 1550, text_manual);

	UG_PutString(120, 1575, "#3: Select an image to upload to EPD");

	free(qrbits_buf);
}

void draw_note(YEPD *epd, uint8_t *fb1)
{
	char note1[] =
		"Use the phone's built-in camera to scan the QR code because most current mobile operating systems restrict third-party applications from controlling Wi-Fi settings directly.";
	char note2[] =
		"After connecting to the Wi-Fi, you may see a message saying that this network has no internet access, or prompting you to use mobile data instead. This is normal, as this project operates locally. Choose to stay on this Wi-Fi network and ignore prompts to switch to mobile data.";

	UG_GUI ug;
	UG_Init(&ug, draw_px_ug_port, EPD_WIDTH, EPD_HEIGHT, fb1);
	UG_FillFrame(0, epd->height - 110, epd->width - 1, epd->height - 1,
		     WHITE);
	UG_SetBackcolor(WHITE);
	UG_SetForecolor(RED);
	UG_FontSelect(&FONT_24X40);

	UG_PutString(50, 100, note1);
	UG_PutString(50, 500, note2);
}

void show_start_screen(YEPD *epd)
{
	show_ram_space("show_start_screen begin");

	uint8_t *fb1 = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbm = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);
	uint8_t *fbs = (uint8_t *)heap_caps_malloc(EPD_HEIGHT * EPD_WIDTH / 4,
						   MALLOC_CAP_SPIRAM);

	show_ram_space("show_start_screen after alloc");

	// compatiable with color pallet
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
	draw_qrcode_on_ram(epd, fb1);

	// fb1 -> fb2
	palette_index_to_E6_data(fb1, fbm, fbs);

	// ppd send data, update
	//EL133UF1_Init();
	//EL133UF1_DisplayFrame(fbm, fbs);
	//EL133UF1_Deinit();

	free(fbs);
	free(fbm);
	free(fb1);

	show_ram_space("show_start_screen before exit");
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