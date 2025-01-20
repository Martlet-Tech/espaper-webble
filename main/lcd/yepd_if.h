//=================================================================================================
//                   Communication interface for 31.5" Control Board
//
// File Name : comm.h
// Author : Electronic Design Dept. II
// Data : 2023.11.13
// Version : 1.0
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __YEPD_IF_H__
#define __YEPD_IF_H__

#include "yepd.h"

#define CS_MASK_MASTER 0x01
#define CS_MASK_SLAVE 0x10
#define CS_MASK_ALL 0x11

#define CHUNK_SIZE 4096 * 2

void epd_wbyte_multi(const unsigned char *bytes, unsigned int length);
void epd_wbyte(const unsigned char data);

void yepd_write(YEPD *epd, uint8_t *cmd, size_t cmd_len, uint8_t *data,
		size_t data_len);
void yepd_execute_sequence(YEPD *epd, const char *sequence);

#endif // #ifndef __COMM_H__
