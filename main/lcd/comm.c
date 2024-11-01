//=================================================================================================
//                   Communication interface for 31.5" Control Board
//
// File Name : comm.c
// Author : Electronic Design Dept. II
// Data : 2023.11.13
// Version : 1.0
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#define __COMM_C__

#include <stdio.h>
#include <string.h>
//======= Please modify the h files according to your MCU API ========
// #include "i2c.h"
// #include "spi.h"
// #include "gpoi.h"
// #include "delay.h"
//================================================================
#include "comm.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "pindefine.h"

#define TAG "EPD-COMM"

extern spi_device_handle_t spi;

void delayms(unsigned int delayTime)
{
	vTaskDelay(delayTime / portTICK_PERIOD_MS);
}

void setGpioLevel(unsigned char pinNumber, unsigned char voltageLevel)
{
	gpio_set_level(pinNumber, voltageLevel);
}

unsigned char getGpioLevel(unsigned char pinNumber)
{
	unsigned char voltageLevel = 0;
	//==== Get GPIO voltage level ====
	voltageLevel = gpio_get_level(pinNumber);

	return voltageLevel;
}

void EPD_IO_WriteDataBytes(const unsigned char *bytes, unsigned int length)
{
	esp_err_t ret;
	spi_transaction_t t;
	const unsigned int chunk_size = CHUNK_SIZE; // 每次传输的最大数据大小（字节）
	unsigned int bytes_left = length;	    // 剩余未传输的数据
	unsigned int offset = 0;		    // 当前偏移量

	while (bytes_left > 0) {
		// 计算本次传输的数据量（最多为 chunk_size 字节）
		unsigned int current_chunk = (bytes_left > chunk_size) ? chunk_size : bytes_left;

		memset(&t, 0, sizeof(t));     // 清空事务结构体
		t.length = 8 * current_chunk; // 当前数据段的长度，单位为 bit
		t.tx_buffer = bytes + offset; // 指向当前要发送的数据段

		// 发送当前数据段
		ret = spi_device_transmit(spi, &t);
		if (ret == ESP_OK) {
			// ESP_LOGI(TAG, "Data chunk sent successfully, bytes_left = %d", bytes_left);
		} else {
			ESP_LOGE(TAG, "Failed to send data chunk, chunk size = %d", current_chunk);
			return; // 如果传输失败，提前退出
		}

		// 更新剩余未传输的数据量和偏移量
		bytes_left -= current_chunk;
		offset += current_chunk;
	}
}

void EPD_IO_Write_byte(const unsigned char data)
{
	EPD_IO_WriteDataBytes(&data, 1);
}

void EPD_IO_WriteCommandData_2CH(const unsigned char cmd, const unsigned char *data, unsigned int data_length, unsigned int cs_mask)
{
	if (cs_mask == CS_MASK_MASTER_SLAVE) {
		setGpioLevel(PIN_CS_M, 0);
		setGpioLevel(PIN_CS_S, 0);
	} else if (cs_mask == CS_MASK_MASTER)
		setGpioLevel(PIN_CS_M, 0);
	else if (cs_mask == CS_MASK_SLAVE)
		setGpioLevel(PIN_CS_S, 0);

	EPD_IO_Write_byte(cmd);
	EPD_IO_WriteDataBytes(data, data_length);

	if (cs_mask == CS_MASK_MASTER_SLAVE) {
		setGpioLevel(PIN_CS_M, 1);
		setGpioLevel(PIN_CS_S, 1);
	} else if (cs_mask == CS_MASK_MASTER)
		setGpioLevel(PIN_CS_M, 1);
	else if (cs_mask == CS_MASK_SLAVE)
		setGpioLevel(PIN_CS_S, 1);
}
