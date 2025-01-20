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
#ifndef __YEPD_H__
#define __YEPD_H__

#include "stdint.h"
#include <stddef.h>

typedef int (*YEPD_Fill_FB)(uint8_t *buff);
typedef int (*YEPD_Initial)(void);
typedef int (*YEPD_Update)(void);

typedef enum {
	YEPD_IF_NOT_DEFINE,
	YEPD_IF_SPI9B, // 9bit msb is dc
	YEPD_IF_SPI8DC, // 8bits with dc line
	YEPD_IF_SPI8S, // standard single
	YEPD_IF_SPIQ, // quad
	YEPD_IF_80_8,
	YEPD_IF_80_16,
	YEPD_IF_80_18,
	YEPD_IF_80_24,
	YEPD_IF_MAX,
} YEPD_IF;

typedef struct {
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;
	uint8_t section_fill_cmd;
	uint8_t cs_mask;
	void (*index_to_section)(uint16_t x0, uint16_t y0, uint16_t x1,
				 uint16_t y1, uint8_t *index_buff,
				 uint8_t *data_buff);
} YEPD_SECTION;

typedef struct {
	char *name;

	uint16_t width;
	uint16_t height;
	const char *palette;
	uint8_t bpp; // in data buffer, how many bits in a pixel
	YEPD_Initial init; // initail gpio, bus, and epd module
	YEPD_Fill_FB fill_index; // fill index buffer
	YEPD_Update update; // update deinitial and sleep

	YEPD_IF interface;
	int pin_rst;
	int pin_busy;
	int pin_cs[8]; // cs pin 0~7, -1 for not use
	int pin_sck;
	int pin_dc;
	int pin_d[24]; // spi9b:0=sda

	YEPD_SECTION sections[8];

	const char *cmd_init;
	const char *cmd_disp;
} YEPD;

YEPD *yepd_find_by_name(const char *module_name);

int yepd_display_index(YEPD *epd, uint8_t *index_buffer);
#endif