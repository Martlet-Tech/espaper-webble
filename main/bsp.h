/**
 * @file bsp.h
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-10
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */
#ifndef __BSP_H__
#define __BSP_H__

#include "stdint.h"
#include "esp_err.h"
#include "yepd.h"

#define PIN_SW46 46
#define PIN_SW3 3
#define GPIO_IO_NUM_1 4 // 修改为实际使用的引脚号
#define GPIO_IO_NUM_2 5
#define GPIO_IO_NUM_3 6
#define GPIO_IO_NUM_4 7
#define GPIO_NUM 4 // 监测的引脚数量
#define DEBOUNCE_TIME_MS 200 // 持续低电平时间（毫秒）
typedef void (*callback_t)(void); // 回调函数类型定义

#define SDCARD_MOUNT_POINT "/sdcard"
#define SPIFFS_MOUNT_POINT "/spiffs"
#define EXAMPLE_MAX_CHAR_SIZE 64

extern YEPD *epd;

void bsp_gpio_initial(void);

void init_spiffs(void);

esp_err_t sdcard_mount(void);
esp_err_t sdcard_unmount(void);
esp_err_t sdcard_test();
uint8_t *SD_MMC_ReadFileToPsram(const char *path, uint32_t *file_size);
esp_err_t write_to_sdcard(const char *filepath, const char *content);
void sdcard_save_buff(uint8_t *buff, int size, const char *file);

esp_err_t bsp_create_wifi_qr_str(char **str_buf);
esp_err_t bsp_create_web_qr_str(char **str_buf);

//=========================================================
int scan_and_sort_images();
int get_max_jpg_num(void);
int set_current_image_number(int num);
int get_file_num_from_index(int index);

#endif
