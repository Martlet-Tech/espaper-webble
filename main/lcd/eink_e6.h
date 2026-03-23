#ifndef __EINK_E6_H__
#define __EINK_E6_H__

#include <stdint.h>

#define EINK_E6_PALETTE "0,0,0;255,255,255;255,255,0;180,0,0;0,0,180;0,180,0"

static inline uint8_t remap_index(uint8_t idx)
{
	return (idx >= 4) ? (idx + 1) : idx;
}

// 对一个打包字节（含两个4bit索引）做重映射
static inline uint8_t remap_packed_byte(uint8_t byte)
{
	uint8_t hi = remap_index((byte >> 4) & 0x0F);
	uint8_t lo = remap_index(byte & 0x0F);
	return (hi << 4) | lo;
}
#endif