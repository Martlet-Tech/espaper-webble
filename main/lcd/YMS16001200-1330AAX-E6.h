
//=================================================================================================
//                   E2803 Driver for 13.3" Control Board
//
// File Name : EL133UF1.h
// Author : Electronic Design Dept. II
// Data : 2023.12.13
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __EL133UF1_H__
#define __EL133UF1_H__

#include "esp_attr.h"
#include "epd.h"

#define BLACK 0x0
#define WHITE 0x1
#define YELLOW 0x2
#define RED 0x3
#define BLUE 0x5
#define GREEN 0x6

#define PSR 0x00
#define PWR 0x01
#define POF 0x02
#define PON 0x04
#define BTST_N 0x05
#define BTST_P 0x06
#define DTM 0x10
#define DRF 0x12
#define CDI 0x50
#define TCON 0x60
#define TRES 0x61
#define AN_TM 0x74
#define AGID 0x86
#define BUCK_BOOST_VDDN 0xB0
#define TFT_VCOM_POWER 0xB1
#define EN_BUF 0xB6
#define BOOST_VDDP_EN 0xB7
#define CCSET 0xE0
#define PWS 0xE3
#define CMD66 0xF0

#define FIRST_DATA_PACKET 1
#define NOT_FIRST_DATA_PACKET 0

// Image buffer for sending image
// MCU RAM Size (800*720/2) reserve for one driver IC
#define EPD_IMAGE_DATA_BUFFER 480000

#define EPD_WIDTH 1200
#define EPD_HEIGHT 1600

// EL133UF1，有两个CS(Frame)每个字节包含两个像素，每个像素4bit
#define EPD_IMAGE_SIZE (EPD_WIDTH * EPD_HEIGHT / 2) // 960,000
#define EPD_FRAME_SIZE (EPD_IMAGE_SIZE / 2) // 480,000

extern unsigned char epdImageDataBuffer[EPD_IMAGE_DATA_BUFFER];

void setPinCsAll(unsigned int setLevel);
void setPinCs(unsigned char csNumber, unsigned int setLevel);
void checkBusyHigh(void);
void checkBusyLow(void);

void EL133UF1_DisplayFrame(const unsigned char *frame_buffer_m,
			   const unsigned char *frame_buffer_s);
void EL133UF1_DisplayColor(unsigned char color, unsigned char *frame_buffer_m,
			   unsigned char *frame_buffer_s);
void EL133UF1_Sleep(void);
int EL133UF1_Deinit(void);

int EL133UF1_Init(void);
int EL133UF1_fill_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
			 uint8_t *rgb_buff);
int EL133UF1_Update(void);
#endif // #ifndef __EL133UF1_H__
