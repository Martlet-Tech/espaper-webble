//=================================================================================================
//                   Communication interface for 31.5" Control Board
//
// File Name : comm.h
// Author : Electronic Design Dept. II
// Data : 2023.11.13
// Version : 1.0
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __COMM_H__
#define __COMM_H__

#define CS_MASK_MASTER 0x01
#define CS_MASK_SLAVE 0x10
#define CS_MASK_MASTER_SLAVE 0x11


void delayms(unsigned int delayTime);
unsigned char spiTransmitCommand(unsigned char commandBuf);
unsigned char spiTransmitData(unsigned char *dataBuffer, unsigned long dataLength);
unsigned char spiTransmitLargeData(unsigned char commandBuf, unsigned char *dataBuffer, unsigned long dataLength);
unsigned char spiTransmit(unsigned char commandBuf, unsigned char *dataBuffer, unsigned int dataLength);
unsigned char spiReceive(unsigned char commandBuf, unsigned char *dataBuffer, unsigned int dataLength);
unsigned char i2cTransmitData(unsigned char i2cAddress, unsigned char *dataBuffer, unsigned int dataLength);
unsigned char i2cReceiveData(unsigned char i2cAddress, unsigned char *dataBuffer, unsigned int dataLength);
void setGpioLevel(unsigned char pinNumber, unsigned char voltageLevel);
unsigned char getGpioLevel(unsigned char pinNumber);

void EPD_IO_WriteDataBytes(const unsigned char *bytes, unsigned int length);
void EPD_IO_Write_byte(const unsigned char data);
void EPD_IO_WriteCommandData_2CH(const unsigned char cmd, const unsigned char *data, unsigned int data_length, unsigned int cs_mask);

#endif // #ifndef __COMM_H__
