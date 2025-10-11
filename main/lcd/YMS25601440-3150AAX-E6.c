//=================================================================================================
//                   EL133UF1 Driver for 13.3" Control Board
//
// File Name : EL133UF1.c
// Author : Electronic Design Dept. II
// Data : 2023.12.13
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include <esp_task_wdt.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include "YMS25601440-3150AAX-E6.h"
#include "bsp.h"
#include "yepd_if.h"
#include "utils.h"
#include "img_proc.h"

#include "IST9201.h"
#include "arduino_wrapper.h"

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
#define DTM 0x10
#define DRF 0x12
#define PLL 0x30
#define TSC 0x40
#define CDI 0x50
#define TCON 0x60
#define TRES 0x61
#define PTLW 0x83
#define CMD66 0xF0
#define VCOM_WOUT_EN 0xB4
#define EN_BUF 0xB6
#define TM_TCON 0xD2
#define CCSET 0xE0
#define PWS 0xE3
#define SPIM 0xE6

//寄存器偏移
#define SET_VPOS_VENG2 2
#define SET_VPOS_VENG3 3
#define SET_VCOMDC 4

//Macros
#define ERROR 1
#define DONE 0

// for partial update
#define PTLW_ENABLE 0x01
#define PTLW_DISABLE 0x00

// Display resolution
#define EPD_WIDTH 2560
#define EPD_HEIGHT 1440
#define EPD_IMAGE_SIZE (EPD_WIDTH * EPD_HEIGHT / 2)

// HGD
#define EPD_FRAME_WIDTH 400
#define EPD_FRAME_HEIGHT 1440
#define EPD_FRAME_COUNT 8
#define EPD_FRAME_SIZE (EPD_FRAME_WIDTH * EPD_FRAME_HEIGHT / 2)

#define EPD_FRAME_OFFSET_0 0
#define EPD_FRAME_OFFSET_1 EPD_FRAME_SIZE
#define EPD_FRAME_OFFSET_2 (EPD_FRAME_OFFSET_1 + EPD_FRAME_SIZE)
#define EPD_FRAME_OFFSET_3 (EPD_FRAME_OFFSET_2 + EPD_FRAME_SIZE)
#define EPD_FRAME_OFFSET_4 (EPD_FRAME_OFFSET_3 + EPD_FRAME_SIZE)
#define EPD_FRAME_OFFSET_5 (EPD_FRAME_OFFSET_4 + EPD_FRAME_SIZE)
#define EPD_FRAME_OFFSET_6 (EPD_FRAME_OFFSET_5 + EPD_FRAME_SIZE)
#define EPD_FRAME_OFFSET_7 (EPD_FRAME_OFFSET_6 + EPD_FRAME_SIZE)
#define EPD_FRAME_BUFFER_SIZE (EPD_FRAME_SIZE * EPD_FRAME_COUNT)

#define BUSY_CHECK_MAX_LOOP 60000
const unsigned char colors[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

extern unsigned char initPmicData[28];

const unsigned char PSR_V[2] = { 0xDB, 0x69 };
const unsigned char PWR_V[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const unsigned char POF_V[1] = { 0x00 };
const unsigned char DRF_V[1] = { 0x01 };
const unsigned char PLL_V[1] = { 0x08 };
const unsigned char CDI_V[1] = { 0xF7 };
const unsigned char TCON_V[2] = { 0x03, 0x03 };
const unsigned char TRES_V[4] = { 0x03, 0x20, 0x02, 0xD0 };
const unsigned char CMD66_V[6] = { 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10 };
const unsigned char VCOM_WOUT_EN_V[6] = { 0x00, 0x00, 0x80, 0xFF, 0xCA, 0x1B };
const unsigned char EN_BUF_V[1] = { 0x00 };
const unsigned char TM_TCON_V[7] = { 0x00, 0xF4, 0x81, 0x40, 0x00, 0x00, 0x00 };
const unsigned char CCSET_V[1] = { 0x01 };
const unsigned char PWS_V[1] = { 0x22 };
const unsigned char SPIM_V[1] = { 0x00 };

static const char TAG[] = "YMS25601440-3150AAX-E6.c";

static uint8_t *dst_frame_buffer; //store hgd processed frames (8 frames)
//static uint8_t *dst_image_buffer; //store image recieved

#if 1 // EPD_IO
#define SPI_MOSI 13
#define SPI_MISO 12
#define SPI_SCLK 14
//#define SPI_SS   17

#define SPI_SI0 SPI_MOSI
#define SPI_SI1 SPI_MISO

// Pin definition
//#define EPD_CS_MR_N   11
#define EPD_CS_SHCP 10
#define EPD_CS_STCP 9
//#define EPD_CS_OE_N   46
#define EPD_CS_DS 3

#define RST_PIN 18
#define BUSY_PIN 8

static const int spiClk = 4000000; // 12 MHz

typedef struct {
	void (*DelayMs)(unsigned int delaytime);
	void (*EPD_IO_Write_byte)(const unsigned char data);
	void (*EPD_IO_WriteDataBytes)(const unsigned char *data,
				      unsigned int count);
	void (*EPD_IO_ReadDataBytes)(unsigned char *data, unsigned int count);

	void (*EPD_IO_Initialize)(void);
	void (*EPD_IO_Deinitialize)(void);
	void (*EPD_IO_Reset)(void);
	void (*EPD_IO_Power_On)(void);
	void (*EPD_IO_Power_Off)(void);

	// void EPD_IO_CS_M_Ctrl(unsigned int status);
	// void EPD_IO_CS_S_Ctrl(unsigned int status);
	void (*EPD_IO_CS_Ctrl)(unsigned int cs, unsigned int on);
	void (*EPD_IO_CS_Ctrl_All)(unsigned int on);
	void (*EPD_IO_WriteCommandData)(const unsigned char cmd,
					const unsigned char *data,
					unsigned int data_length);
	void (*EPD_IO_ReadCommandData)(const unsigned char cmd,
				       unsigned char *data,
				       unsigned int data_length);
	void (*EPD_IO_CheckBusy_L)(void);
	void (*EPD_IO_CheckBusy_H)(void);
} EPD_IO;

void _EPD_IO_CS_Ctrl_All(unsigned int status);

void _EPD_IO_Initialize(void)
{
	//!!
	pinMode(BUSY_PIN, OUTPUT);
	pinMode(RST_PIN, OUTPUT);
	digitalWrite(BUSY_PIN, 0);
	digitalWrite(RST_PIN, 0);

	pinMode(EPD_CS_DS, OUTPUT);
	//pinMode(EPD_CS_OE_N, OUTPUT);
	pinMode(EPD_CS_STCP, OUTPUT);
	pinMode(EPD_CS_SHCP, OUTPUT);
	//pinMode(EPD_CS_MR_N, OUTPUT);

	digitalWrite(EPD_CS_DS, 1);
	//digitalWrite(EPD_CS_OE_N, 0);
	digitalWrite(EPD_CS_STCP, 0);
	digitalWrite(EPD_CS_SHCP, 0);
	//digitalWrite(EPD_CS_MR_N, 1);
	_EPD_IO_CS_Ctrl_All(0);
	//!!

	//IO初始化-SPI
	pinMode(BUSY_PIN, INPUT);
	//!!pinMode(SPI_SCLK, INPUT);
	//!!pinMode(SPI_MISO, INPUT);
	//!!pinMode(SPI_MOSI, INPUT);
	epd_spi.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, -1); //SCLK, MISO, MOSI, SS
	epd_spi.setHwCs(false);
	//!!epd_spi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
	epd_spi.beginTransaction(spiClk, MSBFIRST, SPI_MODE0);

	_EPD_IO_CS_Ctrl_All(0);
}

void _EPD_IO_Deinitialize(void)
{
	epd_spi.endTransaction();
	epd_spi.end();
	//TODO:释放资源、反初始化IO
	_EPD_IO_CS_Ctrl_All(0);
	pinMode(SPI_SCLK, OUTPUT);
	pinMode(SPI_MISO, OUTPUT);
	pinMode(SPI_MOSI, OUTPUT);
	pinMode(BUSY_PIN, OUTPUT);
	digitalWrite(SPI_SCLK, 0);
	digitalWrite(SPI_MISO, 0);
	digitalWrite(SPI_MOSI, 0);
	digitalWrite(BUSY_PIN, 0);
	digitalWrite(RST_PIN, 0);
}

void _EPD_IO_Power_On(void)
{
	//Not Used
}

void _EPD_IO_Power_Off(void)
{
	//Not Used
}

void _DelayMs(unsigned int delaytime)
{
	delay(delaytime);
}

void _EPD_IO_CS_Ctrl(unsigned int cs, unsigned int status)
{
	// digitalWrite(cs_pins[cs], status);
	//74HC595 controls cs pins
	// shift in
	for (int i = 7; i >= 0; i--) {
		if ((cs == i) && (status == LOW)) {
			digitalWrite(EPD_CS_DS, LOW);
		} else {
			digitalWrite(EPD_CS_DS, HIGH);
		}
		digitalWrite(EPD_CS_SHCP, HIGH);
		digitalWrite(EPD_CS_SHCP, LOW);
	}

	//level out
	digitalWrite(EPD_CS_STCP, HIGH);
	digitalWrite(EPD_CS_STCP, LOW);
	// DelayMs(1);
}

void _EPD_IO_CS_Ctrl_All(unsigned int status)
{
	for (int i = 7; i >= 0; i--) {
		digitalWrite(EPD_CS_DS, status);
		digitalWrite(EPD_CS_SHCP, HIGH);
		digitalWrite(EPD_CS_SHCP, LOW);
	}
	digitalWrite(EPD_CS_STCP, HIGH);
	digitalWrite(EPD_CS_STCP, LOW);
}

void _EPD_IO_Write_byte(const unsigned char data)
{
	epd_spi.transferBytes(&data, NULL, 1);
}

void _EPD_IO_WriteDataBytes(const unsigned char *data, unsigned int count)
{
	epd_spi.transferBytes(data, NULL, count);
}

void _EPD_IO_ReadDataBytes(unsigned char *data, unsigned int count)
{
	for (int i = 0; i < count; i++) {
		*data++ = epd_spi.transfer(0xFF);
	}
	// epd_spi.transferBytes(NULL, data, count);
}

/**
       *  @brief: module reset.
       *          often used to awaken the module in deep sleep,
       *          see Epd::Sleep();
       */
void _EPD_IO_Reset(void)
{
	digitalWrite(RST_PIN, LOW); //module reset
	_DelayMs(50);
	digitalWrite(RST_PIN, HIGH);
	_DelayMs(20);
	digitalWrite(RST_PIN, LOW); //module reset
	_DelayMs(50);
	digitalWrite(RST_PIN, HIGH);
	_DelayMs(20);
}

void _EPD_IO_WriteCommandData(const unsigned char cmd,
			      const unsigned char *data,
			      unsigned int data_length)
{
	_EPD_IO_Write_byte(cmd);
	_EPD_IO_WriteDataBytes(data, data_length);
}

void _EPD_IO_ReadCommandData(const unsigned char cmd, unsigned char *data,
			     unsigned int data_length)
{
	_EPD_IO_Write_byte(cmd);
	_EPD_IO_ReadDataBytes(data, data_length);
}

#define BUSY_CHECK_MAX_LOOP 60000

/**
       *  @brief: Wait until the BUSY_PIN goes LOW
       */
void _EPD_IO_CheckBusy_L(void)
{
	int loop_cnt = 0;
	while (digitalRead(BUSY_PIN) == 1) { //1: busy, 0: idle
		_DelayMs(1);
		if ((loop_cnt % 100) == 0) {
			printf("-");
			fflush(stdout);
		}
		if ((loop_cnt++) > BUSY_CHECK_MAX_LOOP) {
			Serial.println("ERROR: L BUSY CHECK Timeout!");
			break;
		}
	}
	printf("\n");
	fflush(stdout);
}

/**
       *  @brief: Wait until the BUSY_PIN goes HIGH
       */
void _EPD_IO_CheckBusy_H(void)
{
	int loop_cnt = 0;
	while (digitalRead(BUSY_PIN) == 0) { //0: busy, 1: idle
		_DelayMs(10);
		if ((loop_cnt % 100) == 0) {
			printf("+");
			fflush(stdout);
		}
		if ((loop_cnt++) > BUSY_CHECK_MAX_LOOP) {
			Serial.println("ERROR: H BUSY CHECK Timeout!");
			break;
		}
	}
	printf("\n");
	fflush(stdout);
}

static EPD_IO epd_io = {
	.DelayMs = _DelayMs,
	.EPD_IO_Write_byte = _EPD_IO_Write_byte,
	.EPD_IO_WriteDataBytes = _EPD_IO_WriteDataBytes,
	.EPD_IO_ReadDataBytes = _EPD_IO_ReadDataBytes,
	.EPD_IO_Initialize = _EPD_IO_Initialize,
	.EPD_IO_Deinitialize = _EPD_IO_Deinitialize,
	.EPD_IO_Reset = _EPD_IO_Reset,
	.EPD_IO_Power_On = _EPD_IO_Power_On,
	.EPD_IO_Power_Off = _EPD_IO_Power_Off,
	.EPD_IO_CS_Ctrl = _EPD_IO_CS_Ctrl,
	.EPD_IO_CS_Ctrl_All = _EPD_IO_CS_Ctrl_All,
	.EPD_IO_WriteCommandData = _EPD_IO_WriteCommandData,
	.EPD_IO_ReadCommandData = _EPD_IO_ReadCommandData,
	.EPD_IO_CheckBusy_L = _EPD_IO_CheckBusy_L,
	.EPD_IO_CheckBusy_H = _EPD_IO_CheckBusy_H,
};

#endif

#if 1 // EL315TW1
//class EL315TW1 {
typedef struct {
	//public:
	//EL315TW1(void);
	// ~EL315TW1(void);

	int (*EL315TW1_Init)(void);
	int (*EL315TW1_CheckDriverICStatus)(void);
	int (*EL315TW1_Deinit)(void);
	void (*EL315TW1_DisplayFrame)(unsigned char *frame_buffer);
	void (*EL315TW1_Update)(void);
	void (*EL315TW1_Sleep)(void);
	void (*EL315TW1_SetPwrToPmic)(unsigned char *pmicData);
	unsigned char (*setEpdPower)(void);
	void (*EL315TW1_TestFunc)(void);
	void (*EL315TW1_DisplayColor)(unsigned char color,
				      unsigned char *frame_buffer);
	void (*EL315TW1_DisplayColorBar)(unsigned char *frame_buffer);
	void (*palette_index_to_EL315_data)(uint8_t *index_buffer,
					    uint8_t *dst);
	void (*palette_index_to_EL315_Sub_data)(uint8_t *index_buffer,
						uint8_t *dst, int width,
						int height);
	void (*EL315TW1_InitPartialUpdateState)(void);
	int (*EL315TW1_SetDisplayAreaForSub)(
		int csx, int x, int y, int width,
		int height); //(x+width <=400), (y+height <= 1440)
	int (*EL315TW1_SendPartialDisplayDataForFub)(uint8_t *data,
						     int data_length);
	void (*EL315TW1_PartialUpdate)(void);
	char (*partialWindowUpdateWithImageData)(
		unsigned char csx, unsigned char const *imageData,
		unsigned long imageDataLength, unsigned int xStart,
		unsigned int yStart, unsigned int xPixel, unsigned int yLine,
		unsigned char epdDisplayEnable);
	char (*partialWindowUpdateWithoutImageData)(
		unsigned char csx, unsigned int xStart, unsigned int yStart,
		unsigned int xPixel, unsigned int yLine,
		unsigned char epdDisplayEnable);

} EL315TW1;

static void _palette_index_to_EL315_data(uint8_t *index_buffer, uint8_t *dst)
{
	// memset(dst, 0x0, EPD_FRAME_BUFFER_SIZE);
	//frame:                0~400, 400~800, 800~1200, 1200~1280, 1280~1680, 1680~2080, 2080~2480, 2480~2560
	//line(width) offset:   0~200, 200~400, 400~600,  600~640,   640~840,   840~1040,  1040~1240, 1240~1280
	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 0; i < 400; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 400; i < 800; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 800; i < 1200; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 1200; i < 1280; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}

		dst += 320;
	}

	//---------------------------------------------------------------------

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 1280; i < 1680; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 1680; i < 2080; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 2080; i < 2480; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}
	}

	for (int j = 0; j < EPD_HEIGHT; j += 2) {
		for (int i = 2480; i < 2560; i++) {
			uint8_t even_byte = index_buffer[i + j * EPD_WIDTH];
			uint8_t odd_byte =
				index_buffer[i + (j + 1) * EPD_WIDTH];
			if (even_byte >= 4)
				even_byte += 1;
			if (odd_byte >= 4)
				odd_byte += 1;
			*dst++ = (odd_byte | ((even_byte << 4) & 0xF0));
		}

		dst += 320;
	}
}

static void EL315TW1_SetPwrToPmic(unsigned char *pmicData)
{
	unsigned int buf;

	buf = ist9201.voltageToRegisterData(pmicData[0],
					    SET_VPOS_VENG2); //VOPS2
	initPmicData[4] = buf / 256;
	initPmicData[5] = buf % 256;
	buf = ist9201.voltageToRegisterData(pmicData[1],
					    SET_VPOS_VENG3); //VPOS3
	initPmicData[8] = buf / 256;
	initPmicData[9] = buf % 256;
	buf = ist9201.voltageToRegisterData(pmicData[2],
					    SET_VPOS_VENG2); //VNEG2
	initPmicData[6] = buf / 256;
	initPmicData[7] = buf % 256;
	buf = ist9201.voltageToRegisterData(pmicData[3],
					    SET_VPOS_VENG3); //VNEG3
	initPmicData[10] = buf / 256;
	initPmicData[11] = buf % 256;
	buf = ist9201.voltageToRegisterData(pmicData[4], SET_VCOMDC); //DCVCOM
	initPmicData[12] = buf / 256;
	initPmicData[13] = buf % 256;
}

static unsigned char setEpdPower(void)
{
	unsigned char i, vcomStatus = DONE;
	unsigned char readTscBuf[2], readPwrBuf[5];

	//Read TSC
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(TSC, &readTscBuf[0], sizeof(readTscBuf));
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	epd_io.EPD_IO_CheckBusy_H();
	// Serial.printf("1-TSC Data = 0x%02X, 0x%02X \r\n", readTscBuf[0], readTscBuf[1]);

	//PON without the external power
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_Write_byte(PON);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	epd_io.EPD_IO_CheckBusy_H();

	//Read PWR
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(0x9B, &readPwrBuf[0], 4);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	// Serial.printf("2-PWR Data = 0x%02X, 0x%02X 0x%02X, 0x%02X \r\n", readPwrBuf[0], readPwrBuf[1], readPwrBuf[2], readPwrBuf[3]);

	//POF
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_Write_byte(POF);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	epd_io.EPD_IO_CheckBusy_H();

	//Read VCOM
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(0x8A, &readPwrBuf[4], 1);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);

	if (readPwrBuf[4] == 0x00) {
		Serial.printf("3-VCOM Data = 0x%02X \r\n", readPwrBuf[4]);
		Serial.printf("Data NG! \r\n");
		vcomStatus = ERROR;
	} else {
		for (i = 0; i < 4; i++) {
			if (readPwrBuf[i] > 120) {
				vcomStatus = ERROR;
				Serial.printf("4-PWM Data [%d] = 0x%02X \r\n",
					      i, readPwrBuf[4]);
				Serial.printf("Data NG! \r\n");
			}
		}

		if (vcomStatus == DONE) {
			readPwrBuf[4] = readPwrBuf[4] - 128;
			EL315TW1_SetPwrToPmic(readPwrBuf);
			// Serial.printf("5-PWM data and VCOM data are both in range! \r\n");
		}
	}
	vcomStatus = DONE;
	return vcomStatus;
}

static void EL315TW1_Update(void)
{
	Serial.println("Turn on Pmic");
	ist9201.enablePmic();

	Serial.println("PON");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_Write_byte(PON);
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	epd_io.EPD_IO_CheckBusy_H();

	Serial.println("DRF");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.DelayMs(10); //30ms
	epd_io.EPD_IO_WriteCommandData(DRF, DRF_V, sizeof(DRF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	epd_io.EPD_IO_CheckBusy_H();

	Serial.println("POF");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(POF, POF_V, sizeof(POF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	epd_io.DelayMs(100);

	Serial.println("Turn Off Pmic");
	ist9201.PowerOffPMIC();
}

static void _EL315TW1_DisplayFrame(unsigned char *frame_buffer)
{
	if (setEpdPower() == DONE) {
		Serial.println("Sending Display Data....");
		for (int i = 0; i < EPD_FRAME_COUNT; i++) {
			epd_io.EPD_IO_CS_Ctrl(i, LOW);
			epd_io.EPD_IO_Write_byte(DTM);
			epd_io.EPD_IO_WriteDataBytes(frame_buffer,
						     EPD_FRAME_SIZE);
			epd_io.EPD_IO_CS_Ctrl(i, HIGH);
			epd_io.DelayMs(1);
			frame_buffer += EPD_FRAME_SIZE;
		}
		Serial.println("Done.");

		EL315TW1_Update();

		Serial.println("Display Frame complete.");
	} else {
		Serial.println(
			"Display Frame does not work due to setEpdPower() NG.");
	}
}

static void _EL315TW1_Sleep(void)
{
	// Serial.println("EL315TW1_Sleep.");
}

static int _EL315TW1_Deinit(void)
{
	//epd_io.EPD_IO_Power_Off();
	// Serial.println("EL315TW1 Power Off.");
	//epd_io.EPD_IO_Deinitialize();
	// Serial.println("EL315TW1 Deinitialize.");
	//ist9201.IfDeinit();
	//TODO
	return 0;
}

static EL315TW1 epd = {
	.palette_index_to_EL315_data = _palette_index_to_EL315_data,
	.EL315TW1_DisplayFrame = _EL315TW1_DisplayFrame,
	.EL315TW1_Sleep = _EL315TW1_Sleep,
	.EL315TW1_Deinit = _EL315TW1_Deinit,
};

static int EL315TW1_CheckDriverICStatus(void)
{
	unsigned char csx, status = DONE;
	unsigned char cmd = 0xF2;
	unsigned char buf[3] = { 0 };

	for (csx = 0; csx < 8; csx++) {
		epd_io.EPD_IO_CS_Ctrl(csx, LOW);
		epd_io.EPD_IO_ReadCommandData(cmd, buf, sizeof(buf));
		epd_io.EPD_IO_CS_Ctrl(csx, HIGH);
		Serial.printf("Driver IC [%d] = 0x%02X 0x%02X 0x%02X \r\n", csx,
			      buf[0], buf[1], buf[2]);

		if ((buf[0] & 0x01) == 0x01) {
			Serial.printf("Driver IC [%d] is ready. \r\n", csx);
			status |= DONE;
		} else {
			Serial.printf("Driver IC [%d] did not reply. \r\n",
				      csx);
			status |= ERROR;
		}
	}
	return status;
}

int EL315TW1_Init(void)
{
	ist9201.IfInit();
	// Serial.println("EL315TW1 Initialize.");
	epd_io.EPD_IO_Initialize();
	// Serial.println("EL315TW1 Power On.");
	epd_io.EPD_IO_Power_On();

	epd_io.EPD_IO_Reset();
	epd_io.EPD_IO_CheckBusy_H();

	do {
		delay(1000);
	} while (EL315TW1_CheckDriverICStatus() != DONE);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(VCOM_WOUT_EN, VCOM_WOUT_EN_V,
				       sizeof(VCOM_WOUT_EN_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TM_TCON, TM_TCON_V, sizeof(TM_TCON_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CMD66, CMD66_V, sizeof(CMD66_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PSR, PSR_V, sizeof(PSR_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PWR, PWR_V, sizeof(PWR_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PLL, PLL_V, sizeof(PLL_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CDI, CDI_V, sizeof(CDI_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TCON, TCON_V, sizeof(TCON_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TRES, TRES_V, sizeof(TRES_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PWS, PWS_V, sizeof(PWS_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	// epd_io.EPD_IO_CS_Ctrl_All(LOW);
	// epd_io.EPD_IO_WriteCommandData(SPIM, SPIM_V, sizeof(SPIM_V));
	// epd_io.EPD_IO_CS_Ctrl_All(HIGH);

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CCSET, CCSET_V, sizeof(CCSET_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	// }
	// if (EL315TW1_CheckDriverICStatus() == DONE) {
	// }
	// Serial.println("EL315TW1 Initialize OK.");
	/* EL315TW1 hardware init end */
	return 0;
}

#endif

static int initial(void)
{
	ESP_LOGI(TAG, "initial start");

	//dst_frame_buffer = (uint8_t *)heap_caps_malloc(EPD_FRAME_BUFFER_SIZE,
	//					       MALLOC_CAP_SPIRAM);
	//if (dst_frame_buffer == NULL) {
	//	ESP_LOGI(TAG,
	//		 "ERROR: memory allcation failed! [dst_frame_buffer]");
	//}

	//dst_image_buffer =
	//	(uint8_t *)heap_caps_malloc(EPD_IMAGE_SIZE, MALLOC_CAP_SPIRAM);
	//if (dst_image_buffer == NULL) {
	//	ESP_LOGI(TAG,
	//		 "ERROR: memory allcation failed! [dst_frame_buffer]");
	//}
	//
	//ESP_LOGI(TAG, "Heap Caps after malloc: %d Bytes\n",
	//	 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

	delay_ms(3000);

	ist9201.DetectHWVersion();

	EL315TW1_Init();

	return 0;
}

static int fill_index_buffer(uint8_t *inbuff)
{
	dst_frame_buffer = (uint8_t *)heap_caps_malloc(EPD_FRAME_BUFFER_SIZE,
						       MALLOC_CAP_SPIRAM);
	if (dst_frame_buffer == NULL) {
		ESP_LOGI(TAG,
			 "ERROR: memory allcation failed! [dst_frame_buffer]");
	}

	epd.palette_index_to_EL315_data(inbuff, dst_frame_buffer);
	Serial.println("EL315TW1 Data Packing Done.");
	epd.EL315TW1_DisplayFrame(dst_frame_buffer);
	Serial.println("EL315TW1 Display Picture Done.");
	epd.EL315TW1_Sleep();
	Serial.println("EL315TW1_Sleep Done.");
	epd.EL315TW1_Deinit();
	Serial.println("EL315TW1_Deinit Done.");

	free(dst_frame_buffer);
	//free(dst_image_buffer);
	return 0;
}

static int update_screen(void)
{
	return 0;
}

YEPD YMS25601440_3150AAX_E6 = {
	.name = "YMS25601440-3150AAX-E6",
	.width = 2560,
	.height = 1440,
	.palette = "0,0,0;255,255,255;255,255,0;255,0,0;0,0,255;0,255,0",
	.init = initial,
	.fill_index = fill_index_buffer,
	.update = update_screen,
};