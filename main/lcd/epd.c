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
		EL133UF1_Init,
		EL133UF1_fill_bitmap,
		EL133UF1_Update,
	},
	{
		"YMS800480-073AAX-E6",
		800,
		480,
		EL133UF1_Init,
		EL133UF1_fill_bitmap,
		EL133UF1_Update,
	},
	{
		"YMS16001200-1330AAX-E6",
		1200,
		1600,
		EL133UF1_Init,
		EL133UF1_fill_bitmap,
		EL133UF1_Update,
	},
	{ NULL, 0, 0, NULL, NULL, NULL },
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

#if 0
static YEPD *yepd_get_by_index(int index)
{
	if (index >= 0 && epd_list[index].name != NULL) {
		return &epd_list[index]; // 返回指向该对象的指针
	}
	return NULL; // 如果索引无效，返回 NULL
}
#endif

YEPD *yepd_init_by_name(const char *module_name)
{
	int index = yepd_get_index_by_name(module_name);
	if (index >= 0) {
		return &epd_list[index];
	}
	return NULL;
}
