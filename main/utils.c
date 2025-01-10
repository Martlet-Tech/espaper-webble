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