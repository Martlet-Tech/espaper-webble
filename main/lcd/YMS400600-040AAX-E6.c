#include "YMS400600-040AAX-E6.h"

#include "utils.h"
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "img_proc.h"

static const char TAG[] = "YMS400600-040AAX-E6";

#define IO_LCD_PWR 46
//#define IO_BS0 37
//#define IO_BS1 36
#define IO_RESETN 45
#define IO_DC 47
#define IO_cSB 21
#define IO_SCLK 14
#define IO_MOSI 13

#define IO_MISO 12
#define IO_BUSY 48

//OUTPUT
//#define DC 	P5OUT_bit.P5OUT0
#define DC_L gpio_set_level(IO_DC, 0)
#define DC_H gpio_set_level(IO_DC, 1)
//#define RSTN  P6OUT_bit.P6OUT1
#define RSTN_L gpio_set_level(IO_RESETN, 0)
#define RSTN_H gpio_set_level(IO_RESETN, 1)
//#define CSB   P6OUT_bit.P6OUT3
#define CSB_L gpio_set_level(IO_cSB, 0)
#define CSB_H gpio_set_level(IO_cSB, 1)
//#define SCL  	P4OUT_bit.P4OUT7
#define SCL_L gpio_set_level(IO_SCLK, 0)
#define SCL_H gpio_set_level(IO_SCLK, 1)
//#define SDA(SI0)  P4OUT_bit.P4OUT6
#define SDA_L gpio_set_level(IO_MOSI, 0)
#define SDA_H gpio_set_level(IO_MOSI, 1)

//#define BS 	P5OUT_bit.P5OUT1
//#define BS_L gpio_set_level(IO_BS0, 0)
//#define BS_H gpio_set_level(IO_BS0, 1)

//#define Switch control  P6OUT_bit.P6OUT0
#define SWC_L //TODO
#define SWC_H //TODO

//========= Input ==========
// UC8154
//#define SDA_IN  P4IN_bit.P4IN6
#define SDA_IN gpio_get_level(IO_MISO)
#define BUSYN gpio_get_level(IO_BUSY)
// MCU Board Button Switch
#define SW2
#define SW3
#define SW4
#define SW5

#define PSR 0x00
#define PWR 0x01
#define POF 0x02
#define POFS 0x03
#define PON 0x04
#define BTST1 0x05
#define BTST2 0x06
#define DSLP 0x07
#define BTST3 0x08
#define DTM 0x10
#define DRF 0x12
#define PLL 0x30
#define CDI 0x50
#define TCON 0x60
#define TRES 0x61
#define REV 0x70
#define VDCS 0x82
#define T_VDCS 0x84
#define PWS 0xE3

static void io_initial(void)
{
	gpio_config_t gpiocfg = {};

	// 初始化 IO_BS0
	gpiocfg.intr_type = GPIO_INTR_DISABLE;
	gpiocfg.mode = GPIO_MODE_OUTPUT;
	gpiocfg.pin_bit_mask = (1ULL << IO_RESETN);
	gpiocfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_LOGI(TAG, "Configuring IO_RESETN");
	gpio_config(&gpiocfg);

	gpiocfg.pin_bit_mask = (1ULL << IO_DC);
	ESP_LOGI(TAG, "Configuring IO_DC");
	gpio_config(&gpiocfg);

	// 初始化 IO_cSB
	gpiocfg.pin_bit_mask = (1ULL << IO_cSB);
	ESP_LOGI(TAG, "Configuring IO_cSB");
	gpio_config(&gpiocfg);
	// 初始化 IO_SCLK
	gpiocfg.pin_bit_mask = (1ULL << IO_SCLK);
	ESP_LOGI(TAG, "Configuring IO_SCLK");
	gpio_config(&gpiocfg);

	// 初始化 IO_MOSI
	gpiocfg.pin_bit_mask = (1ULL << IO_MOSI);
	ESP_LOGI(TAG, "Configuring IO_MOSI");
	gpio_config(&gpiocfg);

	gpio_config_t gpiocfg_in_lcd = {};
	gpiocfg_in_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_in_lcd.mode = GPIO_MODE_INPUT;
	gpiocfg_in_lcd.pin_bit_mask = (1ULL << IO_BUSY);
	gpiocfg_in_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_in_lcd.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&gpiocfg_in_lcd);

	ESP_LOGI(TAG, "io_initial completed");
}

static void spi_9b_init(void)
{
	SCL_L;
	SDA_H;
	CSB_H;
	//BS_H; //3
	DC_L;
	RSTN_H;
	SWC_H;
	delay_ms(10);
}

static void check_busy_high(void) // If BUSYN=0 then waiting
{
	yepd_check_high(IO_BUSY);
}

static void reset(void)
{
	RSTN_L;
	delay_ms(42);
	RSTN_H;
	delay_ms(42);
	RSTN_L;
	delay_ms(42);
	RSTN_H;
	delay_ms(42);
}

static void SPI_COMMAND(unsigned char dat)
{
	unsigned char i;

	CSB_L;

	delay_us(1);
	SDA_L; //0 for DCX_CMD
	SCL_H;
	delay_us(1);
	SCL_L;
	delay_us(1);
	for (i = 0; i < 8; i++) {
		if (dat & 0x80) {
			SDA_H;
		} else {
			SDA_L;
		}
		delay_us(1);
		SCL_H;
		delay_us(1);
		SCL_L;
		dat = dat << 1;
	}
	SDA_L;
	delay_us(1);

	CSB_H;

	delay_us(1);
}

static void SPI_DATA(unsigned char dat)
{
	unsigned char i;

	CSB_L;

	delay_us(1);
	SDA_H; //1 for DCX_DATA
	SCL_H;
	delay_us(1);
	SCL_L;
	delay_us(1);
	for (i = 0; i < 8; i++) {
		if (dat & 0x80) {
			SDA_H;
		} else {
			SDA_L;
		}
		delay_us(1);
		SCL_H;
		delay_us(1);
		SCL_L;
		dat = dat << 1;
	}
	SDA_L;
	delay_us(1);

	CSB_H;

	delay_us(1);
}

#if 0
static void EPD_Display_Black()
{
	unsigned long i;

	SPI_COMMAND(DTM);
	for (i = 0; i < 400 * 600 / 2; i++) {
		if ((i % 1000) == 0) {
			//esp_task_wdt_reset();
			printf(".");
			fflush(stdout);
			//vPortYield();
			delay_ms(1);
		}

		SPI_DATA(0x00);
	}

	SPI_COMMAND(PON);
	check_busy_high();

	//20211212
	//Second setting
	SPI_COMMAND(BTST2);
	SPI_DATA(0x6F);
	SPI_DATA(0x1F);
	SPI_DATA(0x17);
	SPI_DATA(0x49);

	SPI_COMMAND(DRF);
	SPI_DATA(0x00);
	check_busy_high();

	SPI_COMMAND(POF);
	SPI_DATA(0x00);
	check_busy_high();
}
#endif

static void EPD_Init()
{
	//20211212
	SPI_COMMAND(0xAA);
	SPI_DATA(0x49);
	SPI_DATA(0x55);
	SPI_DATA(0x20);
	SPI_DATA(0x08);
	SPI_DATA(0x09);
	SPI_DATA(0x18);

	SPI_COMMAND(PWR);
	SPI_DATA(0x3F);

	SPI_COMMAND(PSR);
	SPI_DATA(0x5F);
	SPI_DATA(0x69);

	SPI_COMMAND(BTST1);
	SPI_DATA(0x40);
	SPI_DATA(0x1F);
	SPI_DATA(0x1F);
	SPI_DATA(0x2C);

	SPI_COMMAND(BTST3);
	SPI_DATA(0x6F);
	SPI_DATA(0x1F);
	SPI_DATA(0x1F);
	SPI_DATA(0x22);

	//===================
	//20211212
	//First setting
	SPI_COMMAND(BTST2);
	SPI_DATA(0x6F);
	SPI_DATA(0x1F);
	SPI_DATA(0x17);
	SPI_DATA(0x17);
	//===================

	SPI_COMMAND(POFS);
	SPI_DATA(0x00);
	SPI_DATA(0x54);
	SPI_DATA(0x00);
	SPI_DATA(0x44);

	SPI_COMMAND(TCON);
	SPI_DATA(0x02);
	SPI_DATA(0x00);
	//Please notice that PLL must be set for version 2 IC
	SPI_COMMAND(PLL);
	SPI_DATA(0x08);

	SPI_COMMAND(CDI);
	SPI_DATA(0x3F);

	SPI_COMMAND(TRES);
	SPI_DATA(0x01);
	SPI_DATA(0x90);
	SPI_DATA(0x02);
	SPI_DATA(0x58);

	SPI_COMMAND(PWS);
	SPI_DATA(0x2F);

	SPI_COMMAND(T_VDCS);
	SPI_DATA(0x01);
}

int YMS400600_040AAX_E6_init(void)
{
	io_initial();
	delay_ms(100);

	spi_9b_init();
	delay_ms(100);

	reset();
	check_busy_high();
	delay_ms(42);

	EPD_Init(); // IC initialization (parameters from Host)
	delay_ms(100);

	ESP_LOGI(TAG, "init end");

	return 0;
}

int YMS400600_040AAX_E6_fill_index(uint8_t *buff)
{
	ESP_LOGI(TAG, "fill start");

	//EPD_Display_Black();

	uint8_t *index_buffer = buff;

	uint32_t data_buff_size = 400 * 600 / 2;

	uint8_t *data_buff = heap_caps_malloc(data_buff_size, MALLOC_CAP_SPIRAM);
	if (data_buff == NULL) {
		ESP_LOGE(TAG, "dst_image_buffer malloc fail");
		return ESP_FAIL;
	}

	uint8_t *epd_ram = data_buff;

	//palette_index_to_E6_data(index_buffer, epd_ram, epd_ram, 800, 480);
	build_data_e6(0, 0, 400, 600, index_buffer, epd_ram);

	SPI_COMMAND(DTM);
	for (int i = 0; i < data_buff_size; i++) {
		if ((i % 1000) == 0) {
			//esp_task_wdt_reset();
			printf(".");
			fflush(stdout);
			//vPortYield();
			delay_ms(1);
		}

		SPI_DATA(*epd_ram++);
	}

	free(data_buff);
	ESP_LOGI(TAG, "fill end");
	return 0;
}

int YMS400600_040AAX_E6_update(void)
{
	ESP_LOGI(TAG, "update start");

	SPI_COMMAND(PON);
	check_busy_high();

	//20211212
	//Second setting
	SPI_COMMAND(BTST2);
	SPI_DATA(0x6F);
	SPI_DATA(0x1F);
	SPI_DATA(0x17);
	SPI_DATA(0x49);

	SPI_COMMAND(DRF);
	SPI_DATA(0x00);
	check_busy_high();

	SPI_COMMAND(POF);
	SPI_DATA(0x00);
	check_busy_high();

	ESP_LOGI(TAG, "update end");
	return 0;
}

static void gpio_initial(void)
{
	gpio_set_direction(IO_LCD_PWR, GPIO_MODE_OUTPUT);
	gpio_set_level(IO_LCD_PWR, 1);
}
static void gpio_deinitial(void)
{
	gpio_set_direction(IO_RESETN, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_DC, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_cSB, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_SCLK, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_MOSI, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_MISO, GPIO_MODE_DISABLE);
	gpio_set_direction(IO_BUSY, GPIO_MODE_DISABLE);

	gpio_set_level(IO_LCD_PWR, 0);
}
static int display_index_buff(uint8_t *buff, size_t size)
{
	ESP_LOGI(TAG, "display_index_buff size %d", size);
	gpio_initial();
	YMS400600_040AAX_E6_init();

	//YMS400600_040AAX_E6_fill_index(buff);
	SPI_COMMAND(DTM);
	for (int i = 0; i < size; i++) {
		if ((i % 1000) == 0) {
			//esp_task_wdt_reset();
			printf(".");
			fflush(stdout);
			delay_ms(1);
		}

		uint8_t high = (buff[i] >> 4) & 0x0F;
		uint8_t low = buff[i] & 0x0F;

		// 2. 核心修正逻辑：如果索引 >= 4，则需要加 1
		if (high >= 4) {
			high++;
		}
		if (low >= 4) {
			low++;
		}

		SPI_DATA((high << 4) | (low & 0x0F));
	}

	YMS400600_040AAX_E6_update();
	delay_ms(1000);
	gpio_deinitial();

	ESP_LOGI(TAG, "display_index_buff end");
	return 0;
}

static int clear_index_buff(uint32_t colorindex)
{
	ESP_LOGI(TAG, "clear_index_buff %08x", colorindex);

	ESP_LOGW(TAG, "display_index_buff NOT implemented");

	ESP_LOGI(TAG, "display_index_buff end");
	return 0;
}

YEPD YMS400600_040AAX_E6 = {
	.name = "YMS400600-040AAX-E6",
	.width = 400,
	.height = 600,
	.palette = "0,0,0;255,255,255;255,255,0;255,0,0;0,0,255;0,255,0",
	.bpp = 4,
	.init = YMS400600_040AAX_E6_init,
	.fill_index = YMS400600_040AAX_E6_fill_index,
	.update = YMS400600_040AAX_E6_update,
	.display_index = display_index_buff,
	.clear = clear_index_buff,
};