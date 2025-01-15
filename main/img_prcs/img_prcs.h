/**
 * @file img_prcs.h
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

#ifndef __IMG_PRCS_H__
#define __IMG_PRCS_H__

#include "esp_err.h"
#include "epd.h"
#include <esp_jpeg_enc.h>

extern int display_debug;

typedef void (*draw_px_func_t)(int16_t x, int16_t y, uint32_t color, void *fb);

void draw_px_ug_port(int16_t x, int16_t y, uint32_t color, void *fb);
void draw_px_24bpp(int16_t x, int16_t y, uint32_t color, void *fb);

void show_start_screen(YEPD *epd);

void draw_qr_code(uint16_t x, uint16_t y, int width_t, int side,
		  uint8_t *bitdata, void *fb, draw_px_func_t draw_px);

void atkinson_dither(uint8_t *image, uint8_t *output_index, int image_width,
		     int image_height, uint8_t **palette, size_t palette_size);
void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s);

jpeg_error_t esp_jpeg_encode_one_picture(uint32_t w, uint32_t h, uint8_t *inbuf,
					 uint8_t *outbuf);

esp_err_t display_palette(YEPD *epd);
esp_err_t display_jpg_file(YEPD *epd, const char *filename);
esp_err_t display_jpg_numble(YEPD *epd, int num);

#endif
