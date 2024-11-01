#include "app.h"

//#include "EL133UF1.h"
//#include "comm.h"
#include "pindefine.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

spi_device_handle_t spi;

void app_gpio_initial(void)
{
	esp_err_t ret;

	spi_bus_config_t bus_config = {
		.mosi_io_num = SPI_Data0_MOSI,
		.miso_io_num = SPI_Data1_MISO,
		.sclk_io_num = SPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = CHUNK_SIZE,
	};
	ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	spi_device_interface_config_t dev_config_0 = {
		.clock_speed_hz = 16000000,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 7,
		.command_bits = 0,
		.address_bits = 0,
		.dummy_bits = 0,
		//.duty_cycle_pos = 128,
		//.flags = SPI_DEVICE_HALFDUPLEX, // 使用半双工模式
	};

	// TEST_ESP_OK(spi_bus_initialize(TEST_SPI_HOST, &buscfg, dma ? SPI_DMA_CH_AUTO : 0));
	ret = spi_bus_add_device(SPI2_HOST, &dev_config_0, &spi);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	gpio_config_t gpiocfg_out_lcd = {};
	gpiocfg_out_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_out_lcd.mode = GPIO_MODE_OUTPUT;
	gpiocfg_out_lcd.pin_bit_mask = (1ULL << EPD_RST) | (1ULL << PIN_CS_M) |
				       (1ULL << PIN_CS_S) | (1ULL << PIN_SW3) |
				       (1ULL << PIN_SW46);
	gpiocfg_out_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_out_lcd.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&gpiocfg_out_lcd);

	gpio_config_t gpiocfg_in_lcd = {};
	gpiocfg_in_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_in_lcd.mode = GPIO_MODE_INPUT;
	gpiocfg_in_lcd.pin_bit_mask = (1ULL << EPD_BUSY);
	gpiocfg_in_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_in_lcd.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&gpiocfg_in_lcd);
}
