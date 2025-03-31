/**
 * @file epd.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-10
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */
#include "yepd.h"
#include "yepd_if.h"
#include "yepd_port.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "YMS400600-040AAX-E6.h"
#include "YMS800480-073AAX-E6.h"
#include "YMS16001200-1330AAX-E6.h"

extern YEPD YMS400600_040AAX_E6;
extern YEPD YMS800480_073AAX_E6;
extern YEPD YMS16001200_1330AAX_E6;

void build_data_e6(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
		   uint8_t *index_buff, uint8_t *data_buff)
{
	uint8_t temp = 0;
	uint8_t index;
	int pixel_count = 0;
	uint16_t width = x1 - x0; // 指定区域的宽度
	//uint16_t height = y1 - y0 + 1; // 指定区域的高度

	// 遍历指定区域内的所有像素
	for (int j = y0; j < y1; j++) {
		for (int i = x0; i < x1; i++) {
			temp <<= 4; // 为新像素腾出位置（左移4位）
			index = index_buff[i + j * width]; // 获取当前像素的索引值
			if (index >= 4) {
				index++; // 根据规则，索引值大于等于4时，需要加1
			}
			temp |= index; // 将索引值合并到 temp 中
			pixel_count++;

			// 每两个像素处理一次，将结果存入 data_buff
			if (pixel_count >= 2) {
				pixel_count = 0;
				*data_buff = temp;
				temp = 0;
				data_buff++;
			}
		}
	}
}

YEPD *epd_list[] = {
	&YMS400600_040AAX_E6,
	&YMS800480_073AAX_E6,
	&YMS16001200_1330AAX_E6,
	NULL,
};

static int yepd_get_index_by_name(const char *module_name)
{
	int index = 0;

	// 遍历 epd_list，直到遇到空对象
	while (epd_list[index] != NULL) {
		if (strcmp(epd_list[index]->name, module_name) == 0) {
			return index; // 找到匹配项，返回索引
		}
		index++;
	}

	return -1; // 未找到，返回 -1
}

YEPD *yepd_find_by_name(const char *module_name)
{
	int index = yepd_get_index_by_name(module_name);
	if (index >= 0) {
		return epd_list[index];
	}
	return NULL;
}

int yepd_display_index(YEPD *epd, uint8_t *index_buffer)
{
	if (index_buffer == NULL) {
		return -1;
	}

	yepd_execute_sequence(epd, epd->cmd_init);

	// process  sections
	for (int i = 0; i < 8; i++) {
		if (epd->sections[i].index_to_section == NULL) {
			break;
		}

		YEPD_SECTION *sec = &(epd->sections[i]);
		uint32_t width = sec->x1 - sec->x0 + 1;
		uint32_t height = sec->y1 - sec->y0 + 1;
		uint32_t pix_cnt = width * height;
		size_t buff_sz = ((epd->bpp > 8) ? (pix_cnt * (epd->bpp / 8)) :
						   (pix_cnt / (8 / epd->bpp)));
		printf("section %d, data buffer size: %zu", i, buff_sz);

		uint8_t *data_buff = (uint8_t *)yepd_malloc(buff_sz);
		sec->index_to_section(sec->x0, sec->y0, sec->x1, sec->y1,
				      index_buffer, data_buff);

		// section selection method?
		for (int i = 0; i < 8; i++) {
			if (sec->cs_mask & (1 << i)) {
				if (epd->pin_cs[i] != -1) {
					gpio_set_level(epd->pin_cs[i], 0);
				}
			}
		}

		// send buff
		yepd_write(epd, &(sec->section_fill_cmd), 1, data_buff,
			   buff_sz);

		// 恢复 CS 引脚高电平（释放）
		for (int i = 0; i < 8; i++) {
			if (sec->cs_mask & (1 << i)) {
				if (epd->pin_cs[i] != -1) {
					gpio_set_level(epd->pin_cs[i], 1);
				}
			}
		}

		yepd_free(data_buff);
	}

	yepd_execute_sequence(epd, epd->cmd_disp);

	return 0;
}

void yepd_check_high(uint32_t pin) // If BUSYN=0 then waiting
{
	uint32_t cnt = 0;
	while (!(gpio_get_level(pin))) {
		vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other tasks
		cnt++;
		if (cnt >= 100) {
			cnt = 0;
			printf("+");
			fflush(stdout); // 手动刷新缓冲区
		}
	};
	printf("\r\n");
}

void yepd_check_low(uint32_t pin) // If BUSYN=1 then waiting
{
	uint32_t cnt = 0;
	while (gpio_get_level(pin)) {
		vTaskDelay(10); // Yield to other tasks
		cnt++;
		if (cnt >= 100) {
			cnt = 0;
			printf("-");
			fflush(stdout); // 手动刷新缓冲区
		}
	};
}