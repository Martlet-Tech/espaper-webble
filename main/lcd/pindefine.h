//=================================================================================================
//                   Pin Define for 31.5" Control Board
//
// File Name : pindefine.h
// Author : Electronic Design Dept. II
// Data : 2023.11.13
// Version : 1.0
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __PINDEFINE_H__
#define __PINDEFINE_H__

//==============  Standard SPI Setting   ==============//
// Please modify the pin number
#define PIN_CS_M 13
#define PIN_CS_S 9
#define SPI_CLK 12
#define SPI_Data0_MOSI 11
#define SPI_Data1_MISO 10
#define SPI_Data2 0
#define SPI_Data3 0

//==============   GPIO Setting   ==============//
// Please modify the pin number
#define EPD_BUSY 14
#define EPD_RST 21

#define PIN_SW46 46
#define PIN_SW3 3

//===============================================

#define GPIO_LOW 0
#define GPIO_HIGH 1

#endif // #ifndef __PINDEFINE_H__
