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
#include <driver/i2c.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "YMS25601440-3150AAX-E6.h"
#include "bsp.h"
#include "yepd_if.h"
#include "utils.h"
#include "img_proc.h"

#include "IST9201.h"

#ifndef LOW
#define LOW 0
#define HIGH 1
#endif

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

#define EPD_SPI_HOST SPI3_HOST

static spi_device_handle_t s_epd_spi_dev;
static bool s_epd_spi_bus_inited;

typedef enum {
	EPD_GPIO_IN = 0,
	EPD_GPIO_OUT,
} epd_gpio_dir_t;

static void epd_gpio_config(int pin, epd_gpio_dir_t dir)
{
	gpio_config_t io = { .pin_bit_mask = 1ULL << pin,
			     .mode = (dir == EPD_GPIO_OUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
			     .pull_up_en = GPIO_PULLUP_DISABLE,
			     .pull_down_en = GPIO_PULLDOWN_DISABLE,
			     .intr_type = GPIO_INTR_DISABLE };
	ESP_ERROR_CHECK(gpio_config(&io));
}

static void epd_spi_bus_ensure_init(int sclk, int miso, int mosi)
{
	if (s_epd_spi_bus_inited) {
		return;
	}
	spi_bus_config_t buscfg = { .mosi_io_num = mosi,
				    .miso_io_num = miso,
				    .sclk_io_num = sclk,
				    .quadwp_io_num = -1,
				    .quadhd_io_num = -1,
				    .max_transfer_sz = 4096 };
	ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
	s_epd_spi_bus_inited = true;
}

static void epd_spi_remove_device(void)
{
	if (s_epd_spi_dev != NULL) {
		spi_bus_remove_device(s_epd_spi_dev);
		s_epd_spi_dev = NULL;
	}
}

static void epd_spi_begin_transaction(uint32_t clock_hz, uint8_t bit_order_msb, uint8_t data_mode)
{
	epd_spi_remove_device();
	spi_device_interface_config_t devcfg = {
		.mode = data_mode,
		.clock_speed_hz = (int)clock_hz,
		.spics_io_num = -1,
		.queue_size = 1,
		.flags = (bit_order_msb == 0) ? (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST) : 0,
		.input_delay_ns = 0,
	};
	ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_epd_spi_dev));
}

static uint8_t epd_spi_transfer_u8(uint8_t data)
{
	spi_transaction_t t = {
		.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
		.length = 8,
		.tx_data = { data },
		.rx_data = { 0 },
	};
	if (spi_device_polling_transmit(s_epd_spi_dev, &t) != ESP_OK) {
		return 0xFF;
	}
	return t.rx_data[0];
}

static void epd_spi_transfer_bytes(const uint8_t *tx, uint8_t *rx, size_t len)
{
	size_t max_chunk = SOC_SPI_MAXIMUM_BUFFER_SIZE;
	size_t transferred = 0;
	int chunk_cnt = 0;
	size_t total = len;

	while (len > 0) {
		size_t chunk = (len > max_chunk) ? max_chunk : len;
		spi_transaction_t t = {
			.length = chunk * 8,
			.tx_buffer = tx ? tx + transferred : NULL,
			.rx_buffer = rx ? rx + transferred : NULL,
		};
		spi_device_transmit(s_epd_spi_dev, &t);
		transferred += chunk;
		len -= chunk;
		if ((transferred % 40000) == 0) {
			printf(".");
			fflush(stdout);
		}
		chunk_cnt++;
	}
	if (chunk_cnt > 1) {
		printf(" [%d chunks]\n", chunk_cnt);
		fflush(stdout);
	}
}

static void epd_spi_shutdown_device(void)
{
	epd_spi_remove_device();
	if (s_epd_spi_bus_inited) {
		spi_bus_free(EPD_SPI_HOST);
		s_epd_spi_bus_inited = false;
	}
}

typedef struct {
	void (*DelayMs)(unsigned int delaytime);
	void (*EPD_IO_Write_byte)(const unsigned char data);
	void (*EPD_IO_WriteDataBytes)(const unsigned char *data, unsigned int count);
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
	void (*EPD_IO_WriteCommandData)(const unsigned char cmd, const unsigned char *data, unsigned int data_length);
	void (*EPD_IO_ReadCommandData)(const unsigned char cmd, unsigned char *data, unsigned int data_length);
	void (*EPD_IO_CheckBusy_L)(void);
	void (*EPD_IO_CheckBusy_H)(void);
} EPD_IO;

void _EPD_IO_CS_Ctrl_All(unsigned int status);

void _EPD_IO_Initialize(void)
{
	epd_gpio_config(RST_PIN, EPD_GPIO_OUT);
	gpio_set_level(RST_PIN, 0);

	epd_gpio_config(EPD_CS_DS, EPD_GPIO_OUT);
	epd_gpio_config(EPD_CS_STCP, EPD_GPIO_OUT);
	epd_gpio_config(EPD_CS_SHCP, EPD_GPIO_OUT);

	gpio_set_level(EPD_CS_DS, 1);
	gpio_set_level(EPD_CS_STCP, 0);
	gpio_set_level(EPD_CS_SHCP, 0);
	_EPD_IO_CS_Ctrl_All(0);

	epd_gpio_config(BUSY_PIN, EPD_GPIO_IN);
	ESP_LOGI(TAG, "_Initialize: after gpio config, BUSY_PIN=%d", gpio_get_level(BUSY_PIN));
	epd_spi_bus_ensure_init(SPI_SCLK, SPI_MISO, SPI_MOSI);
	/* MSB first, SPI mode 0 (CPOL=0, CPHA=0) */
	epd_spi_begin_transaction((uint32_t)spiClk, 1, 0);

	_EPD_IO_CS_Ctrl_All(0);
	ESP_LOGI(TAG, "_Initialize: done, BUSY_PIN=%d", gpio_get_level(BUSY_PIN));
}

void _EPD_IO_Deinitialize(void)
{
	epd_spi_shutdown_device();
	_EPD_IO_CS_Ctrl_All(0);
	epd_gpio_config(SPI_SCLK, EPD_GPIO_OUT);
	epd_gpio_config(SPI_MISO, EPD_GPIO_OUT);
	epd_gpio_config(SPI_MOSI, EPD_GPIO_OUT);
	epd_gpio_config(BUSY_PIN, EPD_GPIO_OUT);
	gpio_set_level(SPI_SCLK, 0);
	gpio_set_level(SPI_MISO, 0);
	gpio_set_level(SPI_MOSI, 0);
	gpio_set_level(BUSY_PIN, 0);
	gpio_set_level(RST_PIN, 0);
	/* set 595 control pins to input/pulldown to avoid back-powering unpowered 74HC595 */
	epd_gpio_config(EPD_CS_DS, EPD_GPIO_IN);
	epd_gpio_config(EPD_CS_STCP, EPD_GPIO_IN);
	epd_gpio_config(EPD_CS_SHCP, EPD_GPIO_IN);
	ist9201.IfDeinit();
	ESP_LOGI(TAG, "_Deinitialize: PMIC powered off");
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
	if (delaytime == 0) {
		return;
	}
	vTaskDelay(pdMS_TO_TICKS(delaytime));
}

void _EPD_IO_CS_Ctrl(unsigned int cs, unsigned int status)
{
	// digitalWrite(cs_pins[cs], status);
	//74HC595 controls cs pins
	// shift in
	ESP_LOGV(TAG, "CS_Ctrl: cs=%d, status=%s", cs, status ? "HIGH" : "LOW");
	for (int i = 7; i >= 0; i--) {
		if ((cs == i) && (status == LOW)) {
			gpio_set_level(EPD_CS_DS, LOW);
		} else {
			gpio_set_level(EPD_CS_DS, HIGH);
		}
		gpio_set_level(EPD_CS_SHCP, HIGH);
		gpio_set_level(EPD_CS_SHCP, LOW);
	}

	gpio_set_level(EPD_CS_STCP, HIGH);
	gpio_set_level(EPD_CS_STCP, LOW);
	// DelayMs(1);
}

void _EPD_IO_CS_Ctrl_All(unsigned int status)
{
	for (int i = 7; i >= 0; i--) {
		gpio_set_level(EPD_CS_DS, status);
		gpio_set_level(EPD_CS_SHCP, HIGH);
		gpio_set_level(EPD_CS_SHCP, LOW);
	}
	gpio_set_level(EPD_CS_STCP, HIGH);
	gpio_set_level(EPD_CS_STCP, LOW);
}

void _EPD_IO_Write_byte(const unsigned char data)
{
	epd_spi_transfer_bytes(&data, NULL, 1);
}

void _EPD_IO_WriteDataBytes(const unsigned char *data, unsigned int count)
{
	epd_spi_transfer_bytes(data, NULL, count);
}

void _EPD_IO_ReadDataBytes(unsigned char *data, unsigned int count)
{
	for (int i = 0; i < count; i++) {
		*data++ = epd_spi_transfer_u8(0xFF);
	}
}

/**
       *  @brief: module reset.
       *          often used to awaken the module in deep sleep,
       *          see Epd::Sleep();
       */
void _EPD_IO_Reset(void)
{
	gpio_set_level(RST_PIN, LOW);
	_DelayMs(50);
	gpio_set_level(RST_PIN, HIGH);
	_DelayMs(20);
	gpio_set_level(RST_PIN, LOW);
	_DelayMs(50);
	gpio_set_level(RST_PIN, HIGH);
	_DelayMs(20);
}

void _EPD_IO_WriteCommandData(const unsigned char cmd, const unsigned char *data, unsigned int data_length)
{
	_EPD_IO_Write_byte(cmd);
	_EPD_IO_WriteDataBytes(data, data_length);
}

void _EPD_IO_ReadCommandData(const unsigned char cmd, unsigned char *data, unsigned int data_length)
{
	_EPD_IO_Write_byte(cmd);
	_EPD_IO_ReadDataBytes(data, data_length);
}

/**
       *  @brief: Wait until the BUSY_PIN goes LOW
       */
void _EPD_IO_CheckBusy_L(void)
{
	int loop_cnt = 0;
	ESP_LOGI(TAG, "CheckBusy_L: enter, BUSY_PIN=%d", gpio_get_level(BUSY_PIN));
	while (gpio_get_level(BUSY_PIN) == 1) { //1: busy, 0: idle
		_DelayMs(1);
		if ((loop_cnt % 100) == 0) {
			printf("-");
			fflush(stdout);
		}
		if ((loop_cnt++) > BUSY_CHECK_MAX_LOOP) {
			ESP_LOGW(TAG, "CheckBusy_L: timeout after %d ms, BUSY_PIN=%d", loop_cnt,
				 gpio_get_level(BUSY_PIN));
			break;
		}
	}
	printf("\n");
	fflush(stdout);
	ESP_LOGI(TAG, "CheckBusy_L: exit, waited ~%d ms", loop_cnt);
}

/**
       *  @brief: Wait until the BUSY_PIN goes HIGH
       */
void _EPD_IO_CheckBusy_H(void)
{
	int loop_cnt = 0;
	ESP_LOGI(TAG, "CheckBusy_H: enter, BUSY_PIN=%d", gpio_get_level(BUSY_PIN));
	while (gpio_get_level(BUSY_PIN) == 0) { //0: busy, 1: idle
		_DelayMs(10);
		if ((loop_cnt % 100) == 0) {
			printf("+");
			fflush(stdout);
		}
		if ((loop_cnt++) > BUSY_CHECK_MAX_LOOP) {
			ESP_LOGW(TAG, "CheckBusy_H: timeout after %d ms, BUSY_PIN=%d", loop_cnt * 10,
				 gpio_get_level(BUSY_PIN));
			break;
		}
	}
	printf("\n");
	fflush(stdout);
	ESP_LOGI(TAG, "CheckBusy_H: exit, waited ~%d ms", loop_cnt * 10);
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
	void (*EL315TW1_DisplayColor)(unsigned char color, unsigned char *frame_buffer);
	void (*EL315TW1_DisplayColorBar)(unsigned char *frame_buffer);
	void (*palette_index_to_EL315_data)(uint8_t *index_buffer, uint8_t *dst);
	void (*palette_index_to_EL315_Sub_data)(uint8_t *index_buffer, uint8_t *dst, int width, int height);
	void (*EL315TW1_InitPartialUpdateState)(void);
	int (*EL315TW1_SetDisplayAreaForSub)(int csx, int x, int y, int width,
					     int height); //(x+width <=400), (y+height <= 1440)
	int (*EL315TW1_SendPartialDisplayDataForFub)(uint8_t *data, int data_length);
	void (*EL315TW1_PartialUpdate)(void);
	char (*partialWindowUpdateWithImageData)(unsigned char csx, unsigned char const *imageData,
						 unsigned long imageDataLength, unsigned int xStart,
						 unsigned int yStart, unsigned int xPixel, unsigned int yLine,
						 unsigned char epdDisplayEnable);
	char (*partialWindowUpdateWithoutImageData)(unsigned char csx, unsigned int xStart, unsigned int yStart,
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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
			uint8_t odd_byte = index_buffer[i + (j + 1) * EPD_WIDTH];
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

	ESP_LOGI(TAG, "setEpdPower: start");

	//Read TSC
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(TSC, &readTscBuf[0], sizeof(readTscBuf));
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	ESP_LOGI(TAG, "setEpdPower: TSC=0x%02X,0x%02X", readTscBuf[0], readTscBuf[1]);
	epd_io.EPD_IO_CheckBusy_H();

	//PON without the external power
	ESP_LOGI(TAG, "setEpdPower: send PON");
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_Write_byte(PON);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	epd_io.EPD_IO_CheckBusy_H();
	ESP_LOGI(TAG, "setEpdPower: PON done");

	//Read PWR
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(0x9B, &readPwrBuf[0], 4);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	ESP_LOGI(TAG, "setEpdPower: PWR=0x%02X,0x%02X,0x%02X,0x%02X", readPwrBuf[0], readPwrBuf[1], readPwrBuf[2],
		 readPwrBuf[3]);

	//POF
	ESP_LOGI(TAG, "setEpdPower: send POF");
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_Write_byte(POF);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	epd_io.EPD_IO_CheckBusy_H();
	ESP_LOGI(TAG, "setEpdPower: POF done");

	//Read VCOM
	epd_io.EPD_IO_CS_Ctrl(0, LOW);
	epd_io.EPD_IO_ReadCommandData(0x8A, &readPwrBuf[4], 1);
	epd_io.EPD_IO_CS_Ctrl(0, HIGH);
	ESP_LOGI(TAG, "setEpdPower: VCOM=0x%02X", readPwrBuf[4]);

	if (readPwrBuf[4] == 0x00) {
		ESP_LOGI(TAG, "3-VCOM Data = 0x%02X", readPwrBuf[4]);
		ESP_LOGI(TAG, "Data NG!");
		vcomStatus = ERROR;
	} else {
		for (i = 0; i < 4; i++) {
			if (readPwrBuf[i] > 120) {
				vcomStatus = ERROR;
				ESP_LOGI(TAG, "4-PWM Data [%d] = 0x%02X", i, readPwrBuf[4]);
				ESP_LOGI(TAG, "Data NG!");
			}
		}

		if (vcomStatus == DONE) {
			readPwrBuf[4] = readPwrBuf[4] - 128;
			EL315TW1_SetPwrToPmic(readPwrBuf);
			// Serial.printf("5-PWM data and VCOM data are both in range! \r\n");
		}
	}
	ESP_LOGI(TAG, "setEpdPower: result vcomStatus=%d", vcomStatus);
	return vcomStatus;
}

static void EL315TW1_Update(void)
{
	ESP_LOGI(TAG, "EL315TW1_Update: start");
	ESP_LOGI(TAG, "Turn on Pmic");
	ist9201.enablePmic();
	ESP_LOGI(TAG, "EL315TW1_Update: enablePmic done");

	ESP_LOGI(TAG, "PON");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_Write_byte(PON);
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "EL315TW1_Update: PON sent, waiting busy...");
	epd_io.EPD_IO_CheckBusy_H();
	ESP_LOGI(TAG, "EL315TW1_Update: PON done");

	ESP_LOGI(TAG, "DRF");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.DelayMs(10); //30ms
	epd_io.EPD_IO_WriteCommandData(DRF, DRF_V, sizeof(DRF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "EL315TW1_Update: DRF sent, waiting busy...");
	epd_io.EPD_IO_CheckBusy_H();
	ESP_LOGI(TAG, "EL315TW1_Update: DRF done");

	ESP_LOGI(TAG, "POF");
	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(POF, POF_V, sizeof(POF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	epd_io.DelayMs(100);

	ESP_LOGI(TAG, "Turn Off Pmic");
	ist9201.PowerOffPMIC();
	ESP_LOGI(TAG, "EL315TW1_Update: end");
}

static void _EL315TW1_DisplayFrame(unsigned char *frame_buffer)
{
	ESP_LOGI(TAG, "_EL315TW1_DisplayFrame: start");
	if (setEpdPower() == DONE) {
		ESP_LOGI(TAG, "Sending Display Data....");
		for (int i = 0; i < EPD_FRAME_COUNT; i++) {
			ESP_LOGI(TAG, "  sending CS[%d] data...", i);
			epd_io.EPD_IO_CS_Ctrl(i, LOW);
			epd_io.EPD_IO_Write_byte(DTM);
			epd_io.EPD_IO_WriteDataBytes(frame_buffer, EPD_FRAME_SIZE);
			epd_io.EPD_IO_CS_Ctrl(i, HIGH);
			epd_io.DelayMs(1);
			frame_buffer += EPD_FRAME_SIZE;
			ESP_LOGI(TAG, "  CS[%d] done", i);
		}
		ESP_LOGI(TAG, "Done.");

		EL315TW1_Update();

		ESP_LOGI(TAG, "Display Frame complete.");
	} else {
		ESP_LOGI(TAG, "Display Frame does not work due to setEpdPower() NG.");
		ist9201.PowerOffPMIC();
	}
	ESP_LOGI(TAG, "_EL315TW1_DisplayFrame: end");
}

static void _EL315TW1_Sleep(void)
{
	// Serial.println("EL315TW1_Sleep.");
}

static int _EL315TW1_Deinit(void)
{
	_EPD_IO_Deinitialize();
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
		ESP_LOGI(TAG, "Driver IC [%d] = 0x%02X 0x%02X 0x%02X", csx, buf[0], buf[1], buf[2]);

		if ((buf[0] & 0x01) == 0x01) {
			ESP_LOGI(TAG, "Driver IC [%d] is ready.", csx);
			status |= DONE;
		} else {
			ESP_LOGI(TAG, "Driver IC [%d] did not reply.", csx);
			status |= ERROR;
		}
	}
	return status;
}

int EL315TW1_Init(void)
{
	ESP_LOGI(TAG, "EL315TW1_Init: start");
	ist9201.IfInit();
	ESP_LOGI(TAG, "EL315TW1_Init: PMIC IfInit done");
	// Serial.println("EL315TW1 Initialize.");
	epd_io.EPD_IO_Initialize();
	// Serial.println("EL315TW1 Power On.");
	epd_io.EPD_IO_Power_On();

	epd_io.EPD_IO_Reset();
	ESP_LOGI(TAG, "EL315TW1_Init: after reset, BUSY_PIN=%d", gpio_get_level(BUSY_PIN));

	epd_io.EPD_IO_CheckBusy_H();

	// === DIAG: BUSY 就绪后读 0xF2 检查 DriverIC 状态 ===
	ESP_LOGI(TAG, "DIAG: reading DriverIC 0xF2 after BUSY ready...");
	for (int cs = 0; cs < 8; cs++) {
		unsigned char buf[3] = { 0 };
		epd_io.EPD_IO_CS_Ctrl(cs, LOW);
		epd_io.EPD_IO_ReadCommandData(0xF2, buf, 3);
		epd_io.EPD_IO_CS_Ctrl(cs, HIGH);
		ESP_LOGI(TAG, "  DIAG DriverIC [%d] = 0x%02X 0x%02X 0x%02X", cs, buf[0], buf[1], buf[2]);
	}
	// === DIAG end ===

	ESP_LOGI(TAG, "EL315TW1_Init: checking DriverIC status...");
	int retry = 0;
	do {
		vTaskDelay(pdMS_TO_TICKS(1000));
		retry++;
		if (retry > 10) {
			ESP_LOGE(TAG, "EL315TW1_Init: DriverIC status retry timeout!");
			break;
		}
	} while (EL315TW1_CheckDriverICStatus() != DONE);
	ESP_LOGI(TAG, "EL315TW1_Init: DriverIC status checked, retry=%d", retry);

	ESP_LOGI(TAG, "EL315TW1_Init: sending init commands...");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(VCOM_WOUT_EN, VCOM_WOUT_EN_V, sizeof(VCOM_WOUT_EN_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  VCOM_WOUT_EN done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TM_TCON, TM_TCON_V, sizeof(TM_TCON_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  TM_TCON done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CMD66, CMD66_V, sizeof(CMD66_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  CMD66 done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PSR, PSR_V, sizeof(PSR_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  PSR done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PWR, PWR_V, sizeof(PWR_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  PWR done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PLL, PLL_V, sizeof(PLL_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  PLL done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CDI, CDI_V, sizeof(CDI_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  CDI done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TCON, TCON_V, sizeof(TCON_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  TCON done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(TRES, TRES_V, sizeof(TRES_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  TRES done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  EN_BUF done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(PWS, PWS_V, sizeof(PWS_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  PWS done");

	epd_io.EPD_IO_CS_Ctrl_All(LOW);
	epd_io.EPD_IO_WriteCommandData(CCSET, CCSET_V, sizeof(CCSET_V));
	epd_io.EPD_IO_CS_Ctrl_All(HIGH);
	ESP_LOGI(TAG, "  CCSET done");

	ESP_LOGI(TAG, "EL315TW1_Init: complete");
	return 0;
}

#endif

// ── 打包 4-bit 输入 → 8 CS 帧缓冲 ────────────────────────────
// 网页传来的 buff 是 4-bit 打包（2 像素/字节），水平排列。
// EL315 硬件接收 8 个 CS 通道，各 400 像素宽（CS 3/7 仅 80 像素），
// 垂直方向每两行打包为一字节：高4位=偶数行，低4位=奇数行。
static void packed_to_EL315_frames(uint8_t *packed, uint8_t *dst)
{
	int w = 2560, h = 1440, half_w = w / 2;

	static const int cs_x0[8] = { 0, 400, 800, 1200, 1280, 1680, 2080, 2480 };
	static const int cs_x1[8] = { 400, 800, 1200, 1280, 1680, 2080, 2480, 2560 };

	for (int cs = 0; cs < 8; cs++) {
		int x0 = cs_x0[cs], x1 = cs_x1[cs], cw = x1 - x0, pad = 400 - cw;

		for (int y = 0; y < h; y += 2) {
			for (int x = x0; x < x1; x++) {
				uint8_t pe = packed[x / 2 + y * half_w];
				uint8_t po = packed[x / 2 + (y + 1) * half_w];
				uint8_t pix_even = (x & 1) ? (pe & 0x0F) : ((pe >> 4) & 0x0F);
				uint8_t pix_odd = (x & 1) ? (po & 0x0F) : ((po >> 4) & 0x0F);
				if (pix_even >= 4)
					pix_even++;
				if (pix_odd >= 4)
					pix_odd++;
				*dst++ = (pix_even << 4) | pix_odd;
			}
			memset(dst, 0, pad);
			dst += pad;
		}
	}
}

static int display_index_buff(uint8_t *buff, size_t size)
{
	uint32_t t0 = esp_log_timestamp();
	ESP_LOGI(TAG, "display_index_buff size %d", size);

	static bool hw_detected = false;
	if (!hw_detected) {
		ist9201.DetectHWVersion();
		vTaskDelay(pdMS_TO_TICKS(3000));
		hw_detected = true;
	}

	EL315TW1_Init();
	ESP_LOGI(TAG, "display_index_buff: EL315TW1_Init took %u ms", esp_log_timestamp() - t0);

	dst_frame_buffer = (uint8_t *)heap_caps_malloc(EPD_FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
	if (dst_frame_buffer == NULL) {
		ESP_LOGE(TAG, "dst_frame_buffer malloc fail");
		_EPD_IO_Deinitialize();
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "display_index_buff: malloc done, heap free=%d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

	uint32_t t1 = esp_log_timestamp();
	packed_to_EL315_frames(buff, dst_frame_buffer);
	ESP_LOGI(TAG, "display_index_buff: packed_to_EL315_frames took %u ms", esp_log_timestamp() - t1);

	uint32_t t2 = esp_log_timestamp();
	epd.EL315TW1_DisplayFrame(dst_frame_buffer);
	ESP_LOGI(TAG, "display_index_buff: EL315TW1_DisplayFrame took %u ms", esp_log_timestamp() - t2);

	free(dst_frame_buffer);
	dst_frame_buffer = NULL;

	_EPD_IO_Deinitialize();

	ESP_LOGI(TAG, "display_index_buff end, total %u ms", esp_log_timestamp() - t0);
	return 0;
}

static int initial(void)
{
	ESP_LOGI(TAG, "initial start");

	delay_ms(3000);

	ist9201.DetectHWVersion();
	ESP_LOGI(TAG, "initial: HW version detected");

	EL315TW1_Init();
	ESP_LOGI(TAG, "initial: EL315TW1_Init done");

	return 0;
}

static int fill_index_buffer(uint8_t *inbuff)
{
	dst_frame_buffer = (uint8_t *)heap_caps_malloc(EPD_FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
	if (dst_frame_buffer == NULL) {
		ESP_LOGI(TAG, "ERROR: memory allcation failed! [dst_frame_buffer]");
	}

	epd.palette_index_to_EL315_data(inbuff, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Data Packing Done.");
	epd.EL315TW1_DisplayFrame(dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display Picture Done.");
	epd.EL315TW1_Sleep();
	ESP_LOGI(TAG, "EL315TW1_Sleep Done.");
	epd.EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");

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
	.bpp = 4,
	.init = initial,
	.fill_index = fill_index_buffer,
	.update = update_screen,
	.display_index = display_index_buff,

	.interface = YEPD_IF_SPI8S,
	.pin_rst = 18,
	.pin_busy = 8,
	.pin_cs = { 0, 1, 2, 3, 4, 5, 6, 7 },
	.pin_sck = 14,
	.pin_dc = -1,
	.pin_d = { 13, -1 },
	.sections = { { .index_to_section = NULL } },
};

static void _EL315TW1_DisplayColor(unsigned char color, unsigned char *frame_buffer)
{
	memset(frame_buffer, color, EPD_FRAME_SIZE);

	if (setEpdPower() == DONE) {
		epd_io.EPD_IO_CS_Ctrl_All(LOW);
		epd_io.EPD_IO_Write_byte(DTM);
		epd_io.EPD_IO_WriteDataBytes(frame_buffer, EPD_FRAME_SIZE);
		epd_io.EPD_IO_CS_Ctrl_All(HIGH);

		EL315TW1_Update();
		ESP_LOGI(TAG, "Display Color complete.");
	} else {
		ESP_LOGI(TAG, "Display Frame does not work due to setEpdPower() NG.");
	}
}

static void _EL315TW1_DisplayColorBar(unsigned char *frame_buffer)
{
	memset(frame_buffer + EPD_FRAME_OFFSET_0, RED, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_1, GREEN, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_2, BLUE, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_3, BLACK, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_4, BLACK, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_5, YELLOW, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_6, WHITE, EPD_FRAME_SIZE);
	memset(frame_buffer + EPD_FRAME_OFFSET_7, WHITE, EPD_FRAME_SIZE);

	_EL315TW1_DisplayFrame(frame_buffer);
}

void test_yms25601440_3150aax(void)
{
	ESP_LOGI(TAG, "=== 31.5 standalone color cycle test ===");

	dst_frame_buffer = (uint8_t *)heap_caps_malloc(EPD_FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
	if (dst_frame_buffer == NULL) {
		ESP_LOGE(TAG, "ERROR: memory allocation failed! [dst_frame_buffer]");
		return;
	}
	ESP_LOGI(TAG, "Heap Caps: %d Bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

	ist9201.DetectHWVersion();

	vTaskDelay(pdMS_TO_TICKS(3000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(WHITE, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display WHITE Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(RED, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display RED Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(YELLOW, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display YELLOW Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(BLUE, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display BLUE Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(BLACK, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display BLACK Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(GREEN, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display GREEN Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColorBar(dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display Color Bar Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");
	vTaskDelay(pdMS_TO_TICKS(5000));

	ESP_LOGI(TAG, "EL315TW1 Demo Start.");
	EL315TW1_Init();
	ESP_LOGI(TAG, "EL315TW1 Init Done.");
	_EL315TW1_DisplayColor(WHITE, dst_frame_buffer);
	ESP_LOGI(TAG, "EL315TW1 Display WHITE Done.");
	_EL315TW1_Deinit();
	ESP_LOGI(TAG, "EL315TW1_Deinit Done.");

	free(dst_frame_buffer);
	dst_frame_buffer = NULL;

	ESP_LOGI(TAG, "=== Color cycle test complete ===");
}
