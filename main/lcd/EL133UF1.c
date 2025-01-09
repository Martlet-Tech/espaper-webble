//=================================================================================================
//                   EL133UF1 Driver for 13.3" Control Board
//
// File Name : EL133UF1.c
// Author : Electronic Design Dept. II
// Data : 2023.12.13
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#define __EL133UF1_C__

#include "EL133UF1.h"
#include "comm.h"
#include "esp_log.h"
#include "pindefine.h"
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include <esp_task_wdt.h>

#define TAG "EL122UF1.c"

const unsigned char spiCsPin[2] = { PIN_CS_M, PIN_CS_S };

const unsigned char PSR_V[2] = { 0xDF,
				 0x69 }; // 0x09 -> 0x69 20240319 V1 update.
const unsigned char PWR_V[6] = { 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38 };
const unsigned char POF_V[1] = { 0x00 };
const unsigned char DRF_V[1] = { 0x00 };
const unsigned char CDI_V[1] = { 0xF7 };
const unsigned char TCON_V[2] = { 0x03, 0x03 };
const unsigned char TRES_V[4] = { 0x04, 0xB0, 0x03, 0x20 };
const unsigned char CMD66_V[6] = { 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10 };
const unsigned char EN_BUF_V[1] = { 0x07 };
const unsigned char CCSET_V[1] = { 0x01 };
const unsigned char PWS_V[1] = { 0x22 };
const unsigned char AN_TM_V[9] = { 0xC0, 0x1C, 0x1C, 0xCC, 0xCC,
				   0xCC, 0x15, 0x15, 0x55 };
const unsigned char AGID_V[1] = { 0x10 };
const unsigned char BTST_P_V[2] = { 0xE8, 0x28 };
const unsigned char BOOST_VDDP_EN_V[1] = { 0x01 };
const unsigned char BTST_N_V[2] = { 0xE8, 0x28 };
const unsigned char BUCK_BOOST_VDDN_V[1] = { 0x01 };
const unsigned char TFT_VCOM_POWER_V[1] = { 0x02 };
const unsigned char SPIM_V[1] = {
	0x10
}; // 0 - Single SPI (Default) 1 - Quad SPI

//================== GPIO Setting ====================================
void resetPin(unsigned int pinStatus)
{
	setGpioLevel(EPD_RST, pinStatus);
}

void setPinCsAll(unsigned int setLevel)
{
	unsigned char i;
	// TODO
	for (i = 0; i < sizeof(spiCsPin); i++) {
		setGpioLevel(spiCsPin[i], setLevel);
	}
}

void setPinCs(unsigned char csNumber, unsigned int setLevel)
{
	setGpioLevel(spiCsPin[csNumber], setLevel);
}

void checkBusyHigh(void) // If BUSYN=0 then waiting
{
	uint32_t cnt = 0;
	while (!(getGpioLevel(EPD_BUSY))) {
		vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other tasks
		cnt++;
		if (cnt >= 100) {
			cnt = 0;
			printf("+");
			fflush(stdout); // 手动刷新缓冲区
		}
	};
	printf("\r\n");
}

void checkBusyLow(void) // If BUSYN=1 then waiting
{
	uint32_t cnt = 0;
	while (getGpioLevel(EPD_BUSY)) {
		vTaskDelay(10); // Yield to other tasks
		cnt++;
		if (cnt >= 100) {
			cnt = 0;
			printf("-");
			fflush(stdout); // 手动刷新缓冲区
		}
	};
}
//====================================================================

void epdHardwareReset(void)
{
	resetPin(GPIO_HIGH);
	delayms(30);
	resetPin(GPIO_LOW);
	delayms(30);
	resetPin(GPIO_HIGH);
	delayms(30);
	resetPin(GPIO_LOW);
	delayms(30);
	resetPin(GPIO_HIGH);
	delayms(30);
}

void EL133UF1_Init(void)
{
	setPinCsAll(GPIO_HIGH);
	delayms(20);

	epdHardwareReset();

	checkBusyHigh();
	ESP_LOGI(TAG, "EPD reset ok\r\n");

	EPD_IO_WriteCommandData_2CH(AN_TM, AN_TM_V, sizeof(AN_TM_V),
				    CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(CMD66, CMD66_V, sizeof(CMD66_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(PSR, PSR_V, sizeof(PSR_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(CDI, CDI_V, sizeof(CDI_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(TCON, TCON_V, sizeof(TCON_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(AGID, AGID_V, sizeof(AGID_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(PWS, PWS_V, sizeof(PWS_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(CCSET, CCSET_V, sizeof(CCSET_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(TRES, TRES_V, sizeof(TRES_V),
				    CS_MASK_MASTER_SLAVE);
	EPD_IO_WriteCommandData_2CH(PWR, PWR_V, sizeof(PWR_V), CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V),
				    CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(BTST_P, BTST_P_V, sizeof(BTST_P_V),
				    CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(BOOST_VDDP_EN, BOOST_VDDP_EN_V,
				    sizeof(BOOST_VDDP_EN_V), CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(BTST_N, BTST_N_V, sizeof(BTST_N_V),
				    CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V,
				    sizeof(BUCK_BOOST_VDDN_V), CS_MASK_MASTER);
	EPD_IO_WriteCommandData_2CH(TFT_VCOM_POWER, TFT_VCOM_POWER_V,
				    sizeof(TFT_VCOM_POWER_V), CS_MASK_MASTER);

	ESP_LOGI(TAG, "EPD initial command send done\r\n");
}

void EL133UF1_Update(void)
{
	ESP_LOGI(TAG, "Updating");

	setGpioLevel(PIN_CS_M, 0);
	setGpioLevel(PIN_CS_S, 0);

	EPD_IO_Write_byte(PON);

	setGpioLevel(PIN_CS_M, 1);
	setGpioLevel(PIN_CS_S, 1);
	checkBusyHigh();
	// ATTENTION: check busy 原本放在CS拉高之前。此为同一般SPI接口屏幕的区别，需要测试是否兼容。

	delayms(30);
	EPD_IO_WriteCommandData_2CH(DRF, DRF_V, sizeof(DRF_V),
				    CS_MASK_MASTER_SLAVE);

	checkBusyHigh();
	// ATTENTION: check busy 原本放在CS拉高之前。此为同一般SPI接口屏幕的区别，需要测试是否兼容。
	EPD_IO_WriteCommandData_2CH(POF, POF_V, sizeof(POF_V),
				    CS_MASK_MASTER_SLAVE);

	ESP_LOGI(TAG, "Update Finish");
}

void EL133UF1_DisplayFrame(const unsigned char *frame_buffer_m,
			   const unsigned char *frame_buffer_s)
{
	// epd_io.EPD_IO_WriteCommandData_2CH(SPIM, SPIM_V, sizeof(SPIM_V), CS_MASK_MASTER_SLAVE);
	// ATTENTION: 原本是在一个CS下拉周期里完成命令和数据的发送。此处待测试
	// EPD_IO_CS_M_Ctrl(0);
	setGpioLevel(PIN_CS_M, 0);
	EPD_IO_Write_byte(DTM);
	EPD_IO_WriteDataBytes(frame_buffer_m, EPD_WIDTH * EPD_HEIGHT / 4);
	// EPD_IO_CS_M_Ctrl(1);
	setGpioLevel(PIN_CS_M, 1);

	// EPD_IO_CS_S_Ctrl(0);
	setGpioLevel(PIN_CS_S, 0);
	EPD_IO_Write_byte(DTM);
	EPD_IO_WriteDataBytes(frame_buffer_s, EPD_WIDTH * EPD_HEIGHT / 4);
	// EPD_IO_CS_S_Ctrl(1);
	setGpioLevel(PIN_CS_S, 1);

	EL133UF1_Update();
}

void EL133UF1_DisplayColor(unsigned char color, unsigned char *frame_buffer_m,
			   unsigned char *frame_buffer_s)
{
	color = color + (color << 4);
	ESP_LOGI(TAG, "EL133UF1_DisplayColor Prepare.");
	for (unsigned int i = 0; i < EPD_FRAME_SIZE; i++) {
		frame_buffer_m[i] = color;
		frame_buffer_s[i] = color;
	}
	ESP_LOGI(TAG, "EL133UF1_DisplayColor Ready.");
	EL133UF1_DisplayFrame(frame_buffer_m, frame_buffer_s);
}

void EL133UF1_Sleep(void)
{
	// Serial.println("EL133UF1_Sleep.");
}

int EL133UF1_Deinit(void)
{
	//epd_io.EPD_IO_Power_Off();
	// Serial.println("EL133UF1 Power Off.");
	//epd_io.EPD_IO_Deinitialize();
	// Serial.println("EL133UF1 Deinitialize.");
	return 0;
}