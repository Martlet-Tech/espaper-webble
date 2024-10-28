#ifndef __IMG_PRCS_H__
#define __IMG_PRCS_H__

#include "esp_err.h"

esp_err_t display_jpg_file(const char *filename);

void draw_px_ug_port(int16_t x, int16_t y, uint32_t color, void *fb);

void show_qrcode(void);
void show_start_screen(void);

#endif
