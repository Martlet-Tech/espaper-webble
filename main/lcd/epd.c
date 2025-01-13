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
#include "epd.h"
#include <string.h>

#include "YMS16001200-1330AAX-E6.h"

YEPD epd_list[] = {
	{
		"YMS400600-040AAX-E6",
		400,
		600,
		"0,0,0;255,255,255;255,255,0;255,0,0;0,0,255;0,255,0",
		EL133UF1_new_init,
		EL133UF1_new_fill_index,
		EL133UF1_new_update,
	},
	{
		"YMS800480-073AAX-E6",
		800,
		480,
		"0,0,0;255,255,255;255,255,0;255,0,0;0,0,255;0,255,0",
		EL133UF1_new_init,
		EL133UF1_new_fill_index,
		EL133UF1_new_update,
	},
	{
		"YMS16001200-1330AAX-E6",
		1200,
		1600,
		"0,0,0;255,255,255;255,255,0;255,0,0;0,0,255;0,255,0",
		EL133UF1_new_init,
		EL133UF1_new_fill_index,
		EL133UF1_new_update,
	},
	{ NULL, 0, 0, NULL, NULL, NULL, NULL },
};

static int yepd_get_index_by_name(const char *module_name)
{
	int index = 0;

	// 遍历 epd_list，直到遇到空对象
	while (epd_list[index].name != NULL) {
		if (strcmp(epd_list[index].name, module_name) == 0) {
			return index; // 找到匹配项，返回索引
		}
		index++;
	}

	return -1; // 未找到，返回 -1
}

YEPD *yepd_init_by_name(const char *module_name)
{
	int index = yepd_get_index_by_name(module_name);
	if (index >= 0) {
		return &epd_list[index];
	}
	return NULL;
}
