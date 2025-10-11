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

#include "img_proc.h"
#include "utils.h"
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

static const char *TAG = "IMG_PRCS";
int display_debug = 0;
int show_qr = 0;
char processing_stage[20] = "idle"; // 初始阶段;

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
	*w = jd_header_info.width;
	*h = jd_header_info.height;
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
			      uint8_t *dst_s, uint16_t w, uint16_t h)
{
	uint8_t temp = 0;
	uint8_t index;
	int pixel_count = 0;
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w / 2; i++) {
			temp <<= 4;
			index = index_buffer[i + j * w];
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

		for (int i = w / 2; i < w; i++) {
			temp <<= 4;
			index = index_buffer[i + j * w];
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

int find_color_in_pallette(const char *palette, uint32_t color)
{
	if (palette == NULL) {
		return -1;
	}

	char *copy = strdup(palette);
	if (copy == NULL) {
		return -1;
	}

	int index = 0;
	char *saveptr1;
	char *color_str = strtok_r(copy, ";", &saveptr1);

	while (color_str != NULL) {
		char *saveptr2;
		char *r_str = strtok_r(color_str, ",", &saveptr2);
		char *g_str = r_str ? strtok_r(NULL, ",", &saveptr2) : NULL;
		char *b_str = g_str ? strtok_r(NULL, ",", &saveptr2) : NULL;
		char *extra = strtok_r(NULL, ",", &saveptr2);

		// 检查是否成功分割出三个分量且无多余分量
		if (r_str && g_str && b_str && extra == NULL) {
			int r = atoi(r_str);
			int g = atoi(g_str);
			int b = atoi(b_str);

			// 将每个分量限制在0-255范围内，并组合成颜色值
			uint32_t current_color = ((r & 0xFF) << 16) |
						 ((g & 0xFF) << 8) | (b & 0xFF);

			if (current_color == color) {
				free(copy);
				return index;
			}

			index++; // 仅在有效颜色时增加索引
		}

		color_str = strtok_r(NULL, ";", &saveptr1);
	}

	free(copy);
	return -1;
}

void draw_px_index(int16_t x, int16_t y, uint32_t color, void *fb)
{
	if (fb) {
		((uint8_t *)fb)[y * gyepd->width + x] = 0xFF & color;
	} else {
		ESP_LOGE("draw_px_ug_port", "fb not initial");
	}

	if (((y % 80) == 0) || ((x % 80) == 0)) {
		vPortYield();
	}
}

void draw_qr_code_index(uint16_t x, uint16_t y, int width_t, int side,
			uint8_t *bitdata, void *fb, draw_px_func_t draw_px,
			uint32_t color_bg, uint8_t color_fg)
{
	//PCD8544_Clear();
	int i = 0;
	int j = 0;
	int a = 0;
	int l = 0;
	int n = 0;
	int scale = 1;

	//memset(fb, color_bg, width_t * width_t);

	scale = width_t / side;

	for (i = 0; i < side; i++) {
		for (j = 0; j < side; j++) {
			a = j * side + i;

			if ((bitdata[a / 8] & (1 << (7 - a % 8)))) {
				for (l = 0; l < scale; l++) {
					for (n = 0; n < scale; n++) {
						draw_px(x + scale * i + l,
							y + scale * (j) + n,
							color_fg, fb);
					}
				}
			} else {
				for (l = 0; l < scale; l++) {
					for (n = 0; n < scale; n++) {
						draw_px(x + scale * i + l,
							y + scale * (j) + n,
							color_bg, fb);
					}
				}
			}
		}
	}
}

esp_err_t draw_QR_to_index_buffer(YEPD *epd, uint8_t *index_buffer)
{
	esp_err_t ret = ESP_OK;

	uint8_t index_black = find_color_in_pallette(epd->palette, 0x000000);
	uint8_t index_white = find_color_in_pallette(epd->palette, 0xFFFFFF);

	const int qr_width = 100;
	const int boader = 20;

	int qr_side;

	UG_GUI ug;
	UG_Init(&ug, draw_px_index, epd->width, epd->height, index_buffer);
	UG_FillFrame(0, epd->height - qr_width - boader * 2,
		     qr_width + boader * 2, epd->height - 1, index_white);

	UG_FillFrame(epd->width - qr_width - boader * 2,
		     epd->height - qr_width - boader * 2, epd->width - 1,
		     epd->height - 1, index_white);

	uint8_t *qrbits_buf =
		heap_caps_malloc(QR_MAX_BITDATA, MALLOC_CAP_SPIRAM);

	char *str_wifi;
	if (bsp_create_wifi_qr_str(&str_wifi) == ESP_OK) {
		ESP_LOGI(TAG, "qr string wifi = %s", str_wifi);

		qr_side = qr_encode(QR_LEVEL_M, 0, str_wifi, strlen(str_wifi),
				    qrbits_buf);
		ESP_LOGI(TAG, "qrencode side wifi = %d", qr_side);

		draw_qr_code_index(20, epd->height - 20 - qr_width, qr_width,
				   qr_side, qrbits_buf, index_buffer,
				   draw_px_index, index_white, index_black);

		free(str_wifi);
	}

	char *str_web;
	if (bsp_create_web_qr_str(&str_web) == ESP_OK) {
		ESP_LOGI(TAG, "qr string web = %s", str_web);

		qr_side = qr_encode(QR_LEVEL_M, 0, str_web, strlen(str_web),
				    qrbits_buf);
		ESP_LOGI(TAG, "qrencode side web = %d", qr_side);

		draw_qr_code_index(epd->width - 20 - qr_width,
				   epd->height - 20 - qr_width, qr_width,
				   qr_side, qrbits_buf, index_buffer,
				   draw_px_index, index_white, index_black);

		free(str_web);
	}

	free(qrbits_buf);

	return ret;
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

	ESP_LOGW(TAG, "display jpg file: %s", filename);

	show_ram_space("start of display_jpg_file");

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

	if ((w_img != epd->width) || (h_img != epd->height)) {
		ESP_LOGE(TAG, "image error w:%d h:%d", w_img, h_img);
		free(jpg_file_buff);
		free(rgb_buff);
		return ESP_FAIL;
	}

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

	if (show_qr != 0) {
		draw_QR_to_index_buffer(epd, index_buffer);
	}

	epd->init();
	epd->fill_index(index_buffer);
	epd->update();

	free(index_buffer);
	free(palette); // 释放数组指针
	show_ram_space("end of display_jpg_file");

	return ret;
}
// Need to free buffer after called!!!
esp_err_t display_indexed_buffer(YEPD *epd, char *index_buffer)
{
	if (show_qr != 0) {
		draw_QR_to_index_buffer(epd, (uint8_t *)index_buffer);
	}

	epd->init();
	epd->fill_index((uint8_t *)index_buffer);
	epd->update();

	//free(index_buffer);

	return ESP_OK;
}

esp_err_t display_jpg_numble(YEPD *epd, int num)
{
	esp_err_t ret = ESP_OK;

	char filename[64]; // 确保这个长度足够存储路径字符串
	snprintf(filename, sizeof(filename), "%s/%d.jpg", SDCARD_MOUNT_POINT,
		 num);

	display_jpg_file(epd, filename);

	return ret;
}

esp_err_t display_palette(YEPD *epd)
{
	const char *TAG = "display_palette";
	esp_err_t ret = ESP_OK;

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

	// 生成调色板索引
	int segment_width = epd->width / color_count;
	for (int y = 0; y < epd->height; y++) {
		for (int x = 0; x < epd->width; x++) {
			int segment = x / segment_width;
			if (segment >= color_count) {
				segment = color_count - 1;
			}
			index_buffer[y * epd->width + x] = segment;
		}
	}

	if (show_qr != 0) {
		draw_QR_to_index_buffer(epd, index_buffer);
	}

	epd->init();
	epd->fill_index(index_buffer);
	epd->update();

	free(index_buffer);
	free(palette); // 释放数组指针
	show_ram_space("end of display_palette");

	return ret;
}
