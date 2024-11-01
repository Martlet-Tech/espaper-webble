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

esp_err_t display_jpg_file(const char *filename);

void draw_px_ug_port(int16_t x, int16_t y, uint32_t color, void *fb);

void show_start_screen(void);

#endif
