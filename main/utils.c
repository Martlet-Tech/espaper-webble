/**
 * @file utils.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-10
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */

#include "utils.h"
#include <esp_heap_caps.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_rom_sys.h"
#include <string.h>

void show_ram_space(const char *position_string)
{
	ESP_LOGI("        RAM SPACE",
		 "Free heap : \t%d bytes, Largest block: \t%d \t@ %s",
		 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
		 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
		 position_string);
}

void delayms(unsigned int delayTime)
{
	vTaskDelay(delayTime / portTICK_PERIOD_MS);
}

void delay_ms(unsigned int delayTime)
{
	vTaskDelay(delayTime / portTICK_PERIOD_MS);
}

void delayus(unsigned int delayTime)
{
	esp_rom_delay_us(delayTime);
}

void delay_us(unsigned int delayTime)
{
	esp_rom_delay_us(delayTime);
}

void safe_free(int **ptr)
{
	if (ptr != NULL && *ptr != NULL) {
		free(*ptr); // 释放指针指向的内存
		*ptr = NULL; // 将外部指针置为 NULL
	}
}

// 交换两个像素，大小为 3 字节 (RGB)
void swap_pixels(unsigned char *a, unsigned char *b)
{
	unsigned char temp[3];
	memcpy(temp, a, 3);
	memcpy(a, b, 3);
	memcpy(b, temp, 3);
}