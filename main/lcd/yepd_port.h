#ifndef __YEPD_PORT_H__
#define __YEPD_PORT_H__

#include "esp_heap_caps.h"
#include "driver/gpio.h"

// GPIO 设置宏
#define YEPD_GPIO_SET(pin, level) gpio_set_level(pin, level)
#define YEPD_GPIO_GET(pin) gpio_get_level(pin)
#define YEPD_DELAY_US(us) esp_rom_delay_us(us) // 延时函数，根据需求选择具体实现

inline void *yepd_malloc(size_t n)
{
	return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}

inline void yepd_free(void *p)
{
	heap_caps_free(p);
}

#endif