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

typedef int (*YEPD_Fill_Bitmap)(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
				uint8_t *rgb_buff);
typedef int (*YEPD_Initial)(void);
typedef int (*YEPD_Update)(void);

typedef struct {
	char *name;
	uint16_t width;
	uint16_t height;
	YEPD_Initial init; // initail gpio, bus, and epd module
	YEPD_Fill_Bitmap fill; // fill bitmap
	YEPD_Update update; // update deinitial and sleep
} YEPD;

YEPD *yepd_init_by_name(const char *module_name);

#endif