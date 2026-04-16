/**
 * @file YMS9841304-1248CIH-E5.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2026-01-08
 * 
 * @copyright zhaitao.as@outlook.com (c) 2026
 * 
 */
#define __YMS9841304_C__

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include <esp_task_wdt.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include "YMS16001200-1330AAX-E6.h"
#include "bsp.h"
#include "yepd_if.h"
#include "utils.h"
#include "img_proc.h"
#include "eink_e5.h"

#define PIN_CS_M1 GPIO_NUM_4
#define PIN_BS GPIO_NUM_5
#define PIN_BUSY_M1 GPIO_NUM_6
#define PIN_RESET GPIO_NUM_7
#define PIN_DC GPIO_NUM_8
#define PIN_CS_S1 GPIO_NUM_9
#define PIN_SCK GPIO_NUM_10
#define PIN_MOSI GPIO_NUM_11
#define PIN_CS_S2 GPIO_NUM_12
#define PIN_PWR GPIO_NUM_46
#define PIN_CS_M2 GPIO_NUM_21
#define PIN_BUSY_M2 GPIO_NUM_48

static const char TAG[] = "YMS9841304-1248CIH-E5.c";

void YMS9841304_DisplayFrame(const unsigned char *frame_buffer_m, const unsigned char *frame_buffer_s);
void YMS9841304_DisplayColor(unsigned char color, unsigned char *frame_buffer_m, unsigned char *frame_buffer_s);
void YMS9841304_Sleep(void);
int YMS9841304_Deinit(void);

//===============================================================
#define Source_BITS_M1 648 ///IC端 ?度
#define Gate_BITS_M1 492 //高度
#define _imageSize_M1 Source_BITS_M1 *Gate_BITS_M1 / 4

#define Source_BITS_S1 656 ///IC端 ?度
#define Gate_BITS_S1 492 //高度
#define _imageSize_S1 Source_BITS_S1 *Gate_BITS_S1 / 4

#define Source_BITS_M2 656 ///IC端 ?度
#define Gate_BITS_M2 492 //高度
#define _imageSize_M2 Source_BITS_M2 *Gate_BITS_M2 / 4

#define Source_BITS_S2 648 ///IC端 ?度
#define Gate_BITS_S2 492 //高度
#define _imageSize_S2 Source_BITS_S2 *Gate_BITS_S2 / 4

#define EPD_W21_MOSI_0 gpio_set_level(PIN_MOSI, 0)
#define EPD_W21_MOSI_1 gpio_set_level(PIN_MOSI, 1)

#define EPD_W21_CLK_0 gpio_set_level(PIN_SCK, 0)
#define EPD_W21_CLK_1 gpio_set_level(PIN_SCK, 1)

#define EPD_W21_CS_M1_0 gpio_set_level(PIN_CS_M1, 0)
#define EPD_W21_CS_M1_1 gpio_set_level(PIN_CS_M1, 1)

#define EPD_W21_CS_S1_0 gpio_set_level(PIN_CS_S1, 0)
#define EPD_W21_CS_S1_1 gpio_set_level(PIN_CS_S1, 1)

#define EPD_W21_CS_S2_0 gpio_set_level(PIN_CS_S2, 0)
#define EPD_W21_CS_S2_1 gpio_set_level(PIN_CS_S2, 1)

#define EPD_W21_CS_M2_0 gpio_set_level(PIN_CS_M2, 0)
#define EPD_W21_CS_M2_1 gpio_set_level(PIN_CS_M2, 1)

#define EPD_W21_DC_0 gpio_set_level(PIN_DC, 0)
#define EPD_W21_DC_1 gpio_set_level(PIN_DC, 1)

#define EPD_W21_RST_0 gpio_set_level(PIN_RESET, 0)
#define EPD_W21_RST_1 gpio_set_level(PIN_RESET, 1)

#define EPD_W21_BS_0 gpio_set_level(PIN_BS, 0)
#define EPD_W21_BS_1 gpio_set_level(PIN_BS, 1)

#define isEPD_W21_BUSY_M1 gpio_get_level(PIN_BUSY_M1)
#define isEPD_W21_BUSY_M2 gpio_get_level(PIN_BUSY_M2)

static void gpio_initial(void)
{
	gpio_set_direction(PIN_PWR, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_PWR, 1);
	vTaskDelay(pdMS_TO_TICKS(500));
	ESP_LOGI(TAG, "电源打开");

	gpio_set_direction(PIN_CS_M1, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_BS, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_BUSY_M1, GPIO_MODE_INPUT);
	gpio_set_direction(PIN_RESET, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_CS_S1, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_SCK, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_MOSI, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_CS_S2, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_BUSY_M2, GPIO_MODE_INPUT);
	gpio_set_direction(PIN_CS_M2, GPIO_MODE_OUTPUT);
	vTaskDelay(pdMS_TO_TICKS(10));
	ESP_LOGI(TAG, "YMS9841304 gpio initial finish");
}

static void gpio_deinitial(void)
{
	gpio_set_direction(PIN_CS_M1, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_BS, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_BUSY_M1, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_RESET, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_DC, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_CS_S1, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_SCK, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_MOSI, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_CS_S2, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_BUSY_M2, GPIO_MODE_DISABLE);
	gpio_set_direction(PIN_CS_M2, GPIO_MODE_DISABLE);
	vTaskDelay(pdMS_TO_TICKS(10));
	ESP_LOGI(TAG, "YMS9841304 gpio initial finish");
}

static void LED0_ON(void)
{
}
static void LED0_OFF(void)
{
}

static void driver_delay_xms(uint32_t n)
{
	vTaskDelay(pdMS_TO_TICKS(n));
}

static void EPD_W21_Init(void)
{
	EPD_W21_CS_M1_1;
	EPD_W21_CS_S1_1;
	//   EPD_W21_CS_M2_1;
	//   EPD_W21_CS_S2_1;

	EPD_W21_RST_1; // Module reset
	driver_delay_xms(10);
	EPD_W21_RST_0; // Module reset
	driver_delay_xms(100);
	EPD_W21_RST_1; // Module reset
	driver_delay_xms(100);
}

static void EPD_lcd_chkstatus(void)
{
	while (isEPD_W21_BUSY_M1 == 0) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

static void EPD_lcd_chkstatus1(void)
{
	while (isEPD_W21_BUSY_M2 == 0) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

static void SPI_Write(unsigned char value)
{
	static uint32_t feeddog = 0;

	unsigned char i;

	feeddog++;
	if (feeddog > 1000) {
		vTaskDelay(pdMS_TO_TICKS(10));
		feeddog = 0;
	}

	//SPI_Delay(1);
	for (i = 0; i < 8; i++) {
		EPD_W21_CLK_0;
		//SPI_Delay(1);
		if (value & 0x80)
			EPD_W21_MOSI_1;
		else
			EPD_W21_MOSI_0;
		value = (value << 1);
		//SPI_Delay(1);
		//driver_delay_us(1);
		delay_ns(100);
		EPD_W21_CLK_1;
		//SPI_Delay(1);
	}
}

static void EPD_W21_WriteCMD_M1(unsigned char command)
{
	EPD_W21_CS_M1_0;
	EPD_W21_DC_0; // command write
	SPI_Write(command);
	EPD_W21_CS_M1_1;
}

static void EPD_W21_WriteDATA_M1(unsigned char command)
{
	EPD_W21_CS_M1_0;
	EPD_W21_DC_1; // command write
	SPI_Write(command);
	EPD_W21_CS_M1_1;
}

static void EPD_W21_WriteCMD_S1(unsigned char command)
{
	EPD_W21_CS_S1_0;
	EPD_W21_DC_0; // command write
	SPI_Write(command);
	EPD_W21_CS_S1_1;
}

static void EPD_W21_WriteDATA_S1(unsigned char command)
{
	EPD_W21_CS_S1_0;
	EPD_W21_DC_1; // command write
	SPI_Write(command);
	EPD_W21_CS_S1_1;
}

static void EPD_W21_WriteCMD_ALL(unsigned char command)
{
	//SPI_Delay(1);
	EPD_W21_CS_M1_0;
	EPD_W21_CS_S1_0;
	EPD_W21_CS_M2_0;
	EPD_W21_CS_S2_0;
	//SPI_Delay(1);
	EPD_W21_DC_0; // command write
	//SPI_Delay(1);
	SPI_Write(command);
	//SPI_Delay(1);
	EPD_W21_CS_S2_1;
	EPD_W21_CS_M2_1;
	EPD_W21_CS_S1_1;
	EPD_W21_CS_M1_1;
}



static void EPD_W21_WriteDATA_ALL(unsigned char command)
{
	//SPI_Delay(1);
	EPD_W21_CS_M1_0;
	EPD_W21_CS_S1_0;
	EPD_W21_CS_M2_0;
	EPD_W21_CS_S2_0;
	//SPI_Delay(1);
	EPD_W21_DC_1; // command write
	//SPI_Delay(1);
	SPI_Write(command);
	//SPI_Delay(1);
	EPD_W21_CS_S2_1;
	EPD_W21_CS_M2_1;
	EPD_W21_CS_S1_1;
	EPD_W21_CS_M1_1;
}







static void EPD_W21_WriteCMD_M2(unsigned char command)
{
	//SPI_Delay(1);
	EPD_W21_CS_M2_0;
	//SPI_Delay(1);
	EPD_W21_DC_0; // command write
	//SPI_Delay(1);
	SPI_Write(command);
	//SPI_Delay(1);
	EPD_W21_CS_M2_1;
}

static void EPD_W21_WriteDATA_M2(unsigned char command)
{
	EPD_W21_CS_M2_0;
	EPD_W21_DC_1; // command write
	SPI_Write(command);
	EPD_W21_CS_M2_1;
}

static void EPD_W21_WriteCMD_S2(unsigned char command)
{
	EPD_W21_CS_S2_0;
	EPD_W21_DC_0; // command write
	SPI_Write(command);
	EPD_W21_CS_S2_1;
}

static void EPD_W21_WriteDATA_S2(unsigned char command)
{
	EPD_W21_CS_S2_0;
	EPD_W21_DC_1; // command write
	SPI_Write(command);
	EPD_W21_CS_S2_1;
}

static void RSD_Set(void)
{
	EPD_W21_WriteCMD_ALL(0xFF);
	EPD_W21_WriteDATA_ALL(0xA5); //

	EPD_W21_WriteCMD_ALL(0xCC);
	EPD_W21_WriteDATA_ALL(0x55);
	EPD_W21_WriteDATA_ALL(0xEA);
	EPD_W21_WriteDATA_ALL(0x55);
	EPD_W21_WriteDATA_ALL(0x05);

	EPD_W21_WriteCMD_ALL(0xFF);
	EPD_W21_WriteDATA_ALL(0xE3); //

	EPD_W21_WriteCMD_ALL(0xA0);
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();
}

static void EPD_init(void)
{
	unsigned char temp;
	EPD_W21_Init(); //Electronic paper IC reset
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();

	RSD_Set(); //针对IC自身问题 加的代码

	EPD_W21_WriteCMD_M1(0x40);
	EPD_lcd_chkstatus();
	delay_ms(500);
	// TODO
	temp = 25; //EPD_W21_ReadDATA_M1_temp(); //aa=Temp
	//	CS1   = 1;

	EPD_W21_WriteCMD_M1(0xE3); //Exit Read

	EPD_W21_WriteCMD_ALL(0xe6);
	EPD_W21_WriteDATA_ALL(temp); //

	EPD_W21_WriteCMD_ALL(0xe0);
	EPD_W21_WriteDATA_ALL(0x03); //
	delay_ms(20);
	EPD_W21_WriteCMD_ALL(0xA5);
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();

	EPD_W21_WriteCMD_ALL(0xE0);
	EPD_W21_WriteDATA_ALL(0x01);
	driver_delay_xms(10);
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();

	EPD_W21_WriteCMD_M1(0x00); //panel setting
	EPD_W21_WriteDATA_M1(0x0F); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteDATA_M1(0x29); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	//	EPD_W21_WriteDATA_M1(0x02);		//KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteCMD_S1(0x00); //panel setting
	EPD_W21_WriteDATA_S1(0x0F); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteDATA_S1(0x29); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	//	EPD_W21_WriteDATA_S1(0x02);		//KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	//M2、S2 turn  180
	EPD_W21_WriteCMD_M2(0x00); //panel setting
	EPD_W21_WriteDATA_M2(0x0F); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteDATA_M2(0x29); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	//	EPD_W21_WriteDATA_M2(0x02);		//KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteCMD_S2(0x00); //panel setting
	EPD_W21_WriteDATA_S2(0x0F); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	EPD_W21_WriteDATA_S2(0x29); //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	//	EPD_W21_WriteDATA_S2(0x02);		//KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f

	EPD_W21_WriteCMD_ALL(0x01);
	EPD_W21_WriteDATA_ALL(0x07);
	EPD_W21_WriteDATA_ALL(0x00);
	//	EPD_W21_WriteDATA_ALL (0x28);
	//	EPD_W21_WriteDATA_ALL (0x78);
	//	EPD_W21_WriteDATA_ALL (0x24);
	//	EPD_W21_WriteDATA_ALL (0x2C);

	EPD_W21_WriteCMD_ALL(0x03);
	EPD_W21_WriteDATA_ALL(0x10);
	EPD_W21_WriteDATA_ALL(0x54);
	EPD_W21_WriteDATA_ALL(0x44);

	//	EPD_W21_WriteCMD_M1(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_M1 (0x17);		//A
	//	EPD_W21_WriteDATA_M1 (0x17);		//B
	//	EPD_W21_WriteDATA_M1 (0x39);		//C
	//	EPD_W21_WriteDATA_M1 (0x17);
	//	EPD_W21_WriteCMD_M2(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_M2 (0x17);		//A
	//	EPD_W21_WriteDATA_M2 (0x17);		//B
	//	EPD_W21_WriteDATA_M2 (0x39);		//C
	//	EPD_W21_WriteDATA_M2 (0x17);
	//	EPD_W21_WriteCMD_S1(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_S1 (0x17);		//A
	//	EPD_W21_WriteDATA_S1 (0x17);		//B
	//	EPD_W21_WriteDATA_S1 (0x39);		//C
	//	EPD_W21_WriteDATA_S1 (0x17);
	//	EPD_W21_WriteCMD_S2(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_S2 (0x17);		//A
	//	EPD_W21_WriteDATA_S2 (0x17);		//B
	//	EPD_W21_WriteDATA_S2 (0x39);		//C
	//	EPD_W21_WriteDATA_S2 (0x17);

	//出现白色道 更改BTST
	EPD_W21_WriteCMD_M1(0x06); //booster soft start
	EPD_W21_WriteDATA_M1(0xC0); //A
	EPD_W21_WriteDATA_M1(0xC0); //B
	EPD_W21_WriteDATA_M1(0xC0); //C
	EPD_W21_WriteCMD_M2(0x06); //booster soft start
	EPD_W21_WriteDATA_M2(0xC0); //A
	EPD_W21_WriteDATA_M2(0xC0); //B
	EPD_W21_WriteDATA_M2(0xC0); //C
	EPD_W21_WriteCMD_S1(0x06); //booster soft start
	EPD_W21_WriteDATA_S1(0xC0); //A
	EPD_W21_WriteDATA_S1(0xC0); //B
	EPD_W21_WriteDATA_S1(0xC0); //C
	EPD_W21_WriteCMD_S2(0x06); //booster soft start
	EPD_W21_WriteDATA_S2(0xC0); //A
	EPD_W21_WriteDATA_S2(0xC0); //B
	EPD_W21_WriteDATA_S2(0xC0); //C

	//	EPD_W21_WriteCMD_M1(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_M1 (0x17);		//A
	//	EPD_W21_WriteDATA_M1 (0x17);		//B
	//	EPD_W21_WriteDATA_M1 (0x27);		//C
	//	EPD_W21_WriteDATA_M1 (0x17);
	//	EPD_W21_WriteCMD_M2(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_M2 (0x17);		//A
	//	EPD_W21_WriteDATA_M2 (0x17);		//B
	//	EPD_W21_WriteDATA_M2 (0x27);		//C
	//	EPD_W21_WriteDATA_M2 (0x17);
	//	EPD_W21_WriteCMD_S1(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_S1 (0x17);		//A
	//	EPD_W21_WriteDATA_S1 (0x17);		//B
	//	EPD_W21_WriteDATA_S1 (0x27);		//C
	//	EPD_W21_WriteDATA_S1 (0x17);
	//	EPD_W21_WriteCMD_S2(0x06);         //booster soft start
	//	EPD_W21_WriteDATA_S2 (0x17);		//A
	//	EPD_W21_WriteDATA_S2 (0x17);		//B
	//	EPD_W21_WriteDATA_S2 (0x27);		//C
	//	EPD_W21_WriteDATA_S2 (0x17);

	EPD_W21_WriteCMD_M1(0x30); //panel setting
	EPD_W21_WriteDATA_M1(0x08);

	EPD_W21_WriteCMD_S1(0x30); //panel setting
	EPD_W21_WriteDATA_S1(0x08);

	EPD_W21_WriteCMD_M2(0x30); //panel setting
	EPD_W21_WriteDATA_M2(0x08);

	EPD_W21_WriteCMD_S2(0x30); //panel setting
	EPD_W21_WriteDATA_S2(0x08);

	EPD_W21_WriteCMD_M1(0x82); //panel setting
	EPD_W21_WriteDATA_M1(0X9E);

	EPD_W21_WriteCMD_S1(0x82); //panel setting
	EPD_W21_WriteDATA_S1(0X9E);

	EPD_W21_WriteCMD_M2(0x82); //panel setting
	EPD_W21_WriteDATA_M2(0X9E);

	EPD_W21_WriteCMD_S2(0x82); //panel setting
	EPD_W21_WriteDATA_S2(0X9E);

	EPD_W21_WriteCMD_ALL(0x50); //Vcom and data interval setting
	EPD_W21_WriteDATA_ALL(0x37); //Border KW

	EPD_W21_WriteCMD_ALL(0x60); //TCON
	EPD_W21_WriteDATA_ALL(0x02);
	EPD_W21_WriteDATA_ALL(0x02);

	EPD_W21_WriteCMD_M1(0x61); //resolution setting
	EPD_W21_WriteDATA_M1(0x02);
	EPD_W21_WriteDATA_M1(0x88); //source 648
	EPD_W21_WriteDATA_M1(0x01); //gate 492
	EPD_W21_WriteDATA_M1(0xEC);
	EPD_W21_WriteCMD_S1(0x61); //resolution setting
	EPD_W21_WriteDATA_S1(0x02);
	EPD_W21_WriteDATA_S1(0x90); //source 656
	EPD_W21_WriteDATA_S1(0x01); //gate 492
	EPD_W21_WriteDATA_S1(0xEC);
	EPD_W21_WriteCMD_M2(0x61); //resolution setting
	EPD_W21_WriteDATA_M2(0x02);
	EPD_W21_WriteDATA_M2(0x90); //source 656
	EPD_W21_WriteDATA_M2(0x01); //gate 492
	EPD_W21_WriteDATA_M2(0xEC);
	EPD_W21_WriteCMD_S2(0x61); //resolution setting
	EPD_W21_WriteDATA_S2(0x02);
	EPD_W21_WriteDATA_S2(0x88); //source 648
	EPD_W21_WriteDATA_S2(0x01); //gate 492
	EPD_W21_WriteDATA_S2(0xEC);
	EPD_W21_WriteCMD_ALL(0xE7); //DUSPI
	EPD_W21_WriteDATA_ALL(0x1C);

	EPD_W21_WriteCMD_ALL(0xE3);
	EPD_W21_WriteDATA_ALL(0x77);

	EPD_W21_WriteCMD_ALL(0xE9);
	EPD_W21_WriteDATA_ALL(0x01);

	EPD_W21_WriteCMD_ALL(0xFF); //DUSPI
	EPD_W21_WriteDATA_ALL(0xA5);

	EPD_W21_WriteCMD_ALL(0xEF); //DUSPI
	EPD_W21_WriteDATA_ALL(1);
	EPD_W21_WriteDATA_ALL(50);

	EPD_W21_WriteDATA_ALL(5);
	EPD_W21_WriteDATA_ALL(26);

	EPD_W21_WriteDATA_ALL(10);
	EPD_W21_WriteDATA_ALL(26);

	EPD_W21_WriteDATA_ALL(20);
	EPD_W21_WriteDATA_ALL(13);

	EPD_W21_WriteCMD_ALL(0XDC); //DUSPI
	EPD_W21_WriteDATA_ALL(0X01);

	EPD_W21_WriteCMD_ALL(0XDD); //DUSPI
	EPD_W21_WriteDATA_ALL(1);

	EPD_W21_WriteCMD_ALL(0XDE); //DUSPI
	EPD_W21_WriteDATA_ALL(13);

	EPD_W21_WriteCMD_ALL(0XF9); //DUSPI
	EPD_W21_WriteDATA_ALL(0X01);

	EPD_W21_WriteCMD_ALL(0XDF); //DUSPI
	EPD_W21_WriteDATA_ALL(0X16);

	EPD_W21_WriteCMD_ALL(0XE8); //DUSPI
	EPD_W21_WriteDATA_ALL(0X07);

	EPD_W21_WriteCMD_ALL(0XFF); //DUSPI
	EPD_W21_WriteDATA_ALL(0XE3);
}

//========================================================================================

static void update(void)
{
	ESP_LOGI(TAG, "触发刷屏");

	EPD_W21_WriteCMD_ALL(0x04);
	ESP_LOGI(TAG, "cmd 04 发送完成");
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();
	delay_ms(300);

	EPD_W21_WriteCMD_ALL(0x12);
	EPD_W21_WriteDATA_ALL(1);
	ESP_LOGI(TAG, "cmd 12 发送完成");
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();

	EPD_W21_WriteCMD_ALL(0x02);
	EPD_W21_WriteDATA_ALL(0x00);
	ESP_LOGI(TAG, "cmd 02 发送完成");
	EPD_lcd_chkstatus();
	EPD_lcd_chkstatus1();

	EPD_W21_WriteCMD_ALL(0x07);
	EPD_W21_WriteDATA_ALL(0xA5);
	ESP_LOGI(TAG, "cmd 07 发送完成");

	LED0_OFF();

	gpio_deinitial();

	gpio_set_level(PIN_PWR, 0);
	ESP_LOGI(TAG, "电源关闭");

	ESP_LOGI(TAG, "刷屏结束");

	return;
}

static inline uint8_t reflect_byte_2bpp(uint8_t b)
{
	return ((b & 0x03) << 6) | ((b & 0x0C) << 2) | ((b & 0x30) >> 2) | ((b & 0xC0) >> 6);
}

static int display_index_buff(uint8_t *datas, size_t size)
{
	if (datas == NULL || size != 1304 * 984 / 4)
		return -1;

	gpio_initial();

	EPD_init();
	ESP_LOGI(TAG, "初始化序列发送完成 ");

	int column, row;
	const int STRIDE = 326; // 1304 / 4

	// --- M1 Part (下半部左侧 648*492): 保持原样 ---
	EPD_W21_WriteCMD_M1(0x10);
	for (column = 492; column < 984; column++) {
		for (row = 0; row < 648 / 4; row++) {
			EPD_W21_WriteDATA_M1(datas[row + column * STRIDE]);
		}
	}

	// --- S1 Part (下半部右侧 656*492): 保持原样 ---
	EPD_W21_WriteCMD_S1(0x10);
	for (column = 492; column < 984; column++) {
		for (row = 648 / 4; row < 1304 / 4; row++) {
			EPD_W21_WriteDATA_S1(datas[row + column * STRIDE]);
		}
	}

	// --- M2 Part (上半部右侧 656*492): 旋转 180 度 ---
	// 逻辑：行从 491 倒序到 0，列从右向左倒序取字节并翻转位
	EPD_W21_WriteCMD_M2(0x10);
	for (column = 491; column >= 0; column--) {
		for (row = 1304 / 4 - 1; row >= 648 / 4; row--) {
			uint8_t b = datas[row + column * STRIDE];
			EPD_W21_WriteDATA_M2(reflect_byte_2bpp(b));
		}
	}

	// --- S2 Part (上半部左侧 648*492): 旋转 180 度 ---
	EPD_W21_WriteCMD_S2(0x10);
	for (column = 491; column >= 0; column--) {
		for (row = 648 / 4 - 1; row >= 0; row--) {
			uint8_t b = datas[row + column * STRIDE];
			EPD_W21_WriteDATA_S2(reflect_byte_2bpp(b));
		}
	}

	ESP_LOGI(TAG, "数据填充完毕");
	update();
	return 0;
}

static void display_clear(uint8_t index)
{
	unsigned long i;

	uint8_t byte_to_fill = index | (index << 2) | (index << 4) | (index << 6);

	EPD_W21_WriteCMD_M1(0x10);
	for (i = 0; i < _imageSize_M1; i++) {
		EPD_W21_WriteDATA_M1(byte_to_fill);
	}

	EPD_W21_WriteCMD_S1(0x10);
	for (i = 0; i < _imageSize_S1; i++) {
		EPD_W21_WriteDATA_S1(byte_to_fill);
	}

	EPD_W21_WriteCMD_M2(0x10);
	for (i = 0; i < _imageSize_M2; i++) {
		EPD_W21_WriteDATA_M2(byte_to_fill);
	}

	EPD_W21_WriteCMD_S2(0x10);
	for (i = 0; i < _imageSize_S2; i++) {
		EPD_W21_WriteDATA_S2(byte_to_fill);
	}
}

static int clear_index_buff(uint32_t index)
{
	ESP_LOGI(TAG, "clear_index_buff index %02x", index);

	gpio_initial();

	LED0_ON();
	EPD_init(); //EPD init
	//display_White();
	display_clear(index);
	update();

	ESP_LOGI(TAG, "clear_index_buff finish");

	return 0;
}

static void test_task(void *pvParameter)
{
	clear_index_buff(0x01);
	vTaskDelete(NULL);
}

static void test_YMS9841304_1248CIH_E5(void)
{
	xTaskCreate(test_task, "test_task", 4096, NULL, 5, NULL);

	ESP_LOGI(TAG, "test_YMS9841304_1248CIH_E5 finish");
}

/*
L*	a*	b*	R (红)	G (绿)	B (蓝)	16进制代码	预览
13	9	-12	39	30	48	#271E30	🖤
65	-4	-2	151	158	159	#979E9F	🩶
65	-13	64	185	154	45	#B99A2D	💛
26	38	28	114	40	38	#722826	🤎
31	4	-38	51	74	124	#334A7C	💙
35	-21	10	48	91	74	#305B4A	💚
*/

YEPD YMS9841304_1248CIH_E5_V2 = {
	.name = "YMS9841304-1248CIH-E5-V2",
	.width = 1304,
	.height = 984,
	// black=00 white=01 red=11 yellow=10
	.palette = EINK_E5_PALETTE,
	.bpp = 2,
	.clear = clear_index_buff,
	.display_index = display_index_buff,
	.test = test_YMS9841304_1248CIH_E5,
};
