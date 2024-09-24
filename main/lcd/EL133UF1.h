
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

#define BLACK 0x00
#define WHITE 0x11
#define YELLOW 0x22
#define RED 0x33
#define BLUE 0x55
#define GREEN 0x66

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
#define EPD_IMAGE_DATA_BUFFER 480000 // MCU RAM Size (800*720/2) reserve for one driver IC

#endif // #ifndef __EL133UF1_H__

// Display resolution
#define EPD_WIDTH 1200
#define EPD_HEIGHT 1600
// EL133UF1，有两个CS(Frame)每个字节包含两个像素，每个像素4bit
#define EPD_IMAGE_SIZE (EPD_WIDTH * EPD_HEIGHT / 2) // 960,000
#define EPD_FRAME_SIZE (EPD_IMAGE_SIZE / 2)	    // 480,000

#ifdef __EL133UF1_C__
#define __EL133UF1_EXTERN__ EXT_RAM_BSS_ATTR
#else
#define __EL133UF1_EXTERN__ extern
#endif

__EL133UF1_EXTERN__ unsigned char epdImageDataBuffer[EPD_IMAGE_DATA_BUFFER];


/*__EL133UF1_EXTERN__*/ void epdHardwareReset(void);
/*__EL133UF1_EXTERN__*/ void setPinCsAll(unsigned int setLevel);
/*__EL133UF1_EXTERN__*/ void setPinCs(unsigned char csNumber, unsigned int setLevel);
/*__EL133UF1_EXTERN__*/ void checkBusyHigh(void);
/*__EL133UF1_EXTERN__*/ void checkBusyLow(void);
/*__EL133UF1_EXTERN__*/ void initEPD(void);
/*__EL133UF1_EXTERN__*/ void writeEpd(unsigned char epdCommand, unsigned char *epdData, unsigned int epdDataLength);
/*__EL133UF1_EXTERN__*/ void readEpd(unsigned char epdCommand, unsigned char *epdData, unsigned int epdDataLength);
/*__EL133UF1_EXTERN__*/ void writeEpdCommand(unsigned char epdCommand);
/*__EL133UF1_EXTERN__*/ void writeEpdData(unsigned char *epdData, unsigned int epdDataLength);
/*__EL133UF1_EXTERN__*/ void epdDisplay(void);
/*__EL133UF1_EXTERN__*/ void epdDisplayColor(unsigned char colorSelect);
/*__EL133UF1_EXTERN__*/ void epdDisplayColorBar(void);
/*__EL133UF1_EXTERN__*/ void writeEpdImage(unsigned char csx, unsigned char const *imageData, unsigned long imageDataLength);

void EL133UF1_DisplayColor(unsigned char color, unsigned char *frame_buffer_m, unsigned char *frame_buffer_s);
