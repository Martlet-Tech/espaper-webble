/**
 * @file epd.h
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-10
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */
#ifndef __EPD_H__
#define __EPD_H__

#include "stdint.h"
#include <stddef.h>

typedef int (*fill_bitmap)(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
			   uint8_t *rgb_buff);
typedef void (*draw_pixel_t)(int16_t x, int16_t y, uint32_t color,
			     void *rgb_buff);

typedef struct {
	char *name;
	uint16_t width;
	uint16_t height;
	fill_bitmap disp_rgb;
} YEPD;

YEPD *yepd_init_by_name(const char *module_name);

#endif