/**
 * @file comm.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-15
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp.h"
#include "yepd_if.h"
#include "yepd_port.h"

static const char TAG[] = "YEPD_IF";

extern spi_device_handle_t spi;

void EPD_IO_WriteDataBytes(const unsigned char *bytes, unsigned int length)
{
	esp_err_t ret;
	spi_transaction_t t;
	// 每次传输的最大数据大小（字节）
	const unsigned int chunk_size = CHUNK_SIZE;
	unsigned int bytes_left = length; // 剩余未传输的数据
	unsigned int offset = 0; // 当前偏移量

	while (bytes_left > 0) {
		// 计算本次传输的数据量（最多为 chunk_size 字节）
		unsigned int current_chunk =
			(bytes_left > chunk_size) ? chunk_size : bytes_left;

		memset(&t, 0, sizeof(t)); // 清空事务结构体
		t.length = 8 * current_chunk; // 当前数据段的长度，单位为 bit
		t.tx_buffer = bytes + offset; // 指向当前要发送的数据段

		// 发送当前数据段
		ret = spi_device_transmit(spi, &t);
		if (ret == ESP_OK) {
			// ESP_LOGI(TAG, "Data chunk sent successfully, bytes_left = %d", bytes_left);
		} else {
			ESP_LOGE(TAG,
				 "Failed to send data chunk, chunk size = %d",
				 current_chunk);
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

// GPIO 模拟 SPI 时钟信号
static void spi_gpio_clock_pulse(int clk_pin)
{
	YEPD_GPIO_SET(clk_pin, 1); // 时钟上升沿
	YEPD_DELAY_US(1); // 根据时钟速度调整延时
	YEPD_GPIO_SET(clk_pin, 0); // 时钟下降沿
	YEPD_DELAY_US(1);
}

// GPIO 模拟 SPI 写入一个位
static void spi_gpio_write_bit(int mosi_pin, int clk_pin, bool bit)
{
	YEPD_GPIO_SET(mosi_pin, bit); // 设置 MOSI 引脚
	spi_gpio_clock_pulse(clk_pin); // 触发时钟脉冲
}

// GPIO 模拟 SPI 读取一个位
static bool spi_gpio_read_bit(int miso_pin, int clk_pin)
{
	bool bit;
	YEPD_GPIO_SET(clk_pin, 1); // 时钟上升沿
	YEPD_DELAY_US(1);
	bit = gpio_get_level(miso_pin); // 读取 MISO 引脚电平
	YEPD_GPIO_SET(clk_pin, 0); // 时钟下降沿
	YEPD_DELAY_US(1);
	return bit;
}

// GPIO 模拟 SPI 写入一个字节（8 位）
static void spi_gpio_write_byte(int mosi_pin, int clk_pin, uint8_t byte)
{
	for (int i = 7; i >= 0; i--) {
		spi_gpio_write_bit(mosi_pin, clk_pin, (byte >> i) & 0x01);
	}
}

// GPIO 模拟 SPI 写入一个 9 位数据
static void spi_gpio_write_9bit(int mosi_pin, int clk_pin, uint16_t data)
{
	for (int i = 8; i >= 0; i--) {
		spi_gpio_write_bit(mosi_pin, clk_pin, (data >> i) & 0x01);
	}
}

// GPIO 模拟 SPI 写入多字节
static void spi_gpio_write_buffer(int mosi_pin, int clk_pin,
				  const uint8_t *buffer, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		spi_gpio_write_byte(mosi_pin, clk_pin, buffer[i]);
	}
}

// yepd_write 函数实现
void yepd_write(YEPD *epd, uint8_t *cmd, size_t cmd_len, uint8_t *data,
		size_t data_len)
{
	if (!epd || !cmd || cmd_len == 0) {
		ESP_LOGE(TAG, "Invalid parameters");
		return;
	}

	int clk_pin = epd->pin_cs[0]; // 使用 epd 中配置的时钟引脚
	int mosi_pin = epd->pin_busy; // 使用 epd 中配置的 MOSI 引脚
	int miso_pin = epd->pin_rst; // 使用 epd 中配置的 MISO 引脚
	int dc_pin = epd->pin_cs[1]; // 使用 epd 中配置的 DC 引脚（可选）

	switch (epd->interface) {
	case YEPD_IF_SPI9B: {
		ESP_LOGI(TAG, "YEPD_IF_SPI9B");
		for (size_t i = 0; i < cmd_len; i++) {
			uint16_t cmd_with_dc = ((uint16_t)cmd[i]) |
					       (1 << 8); // MSB 表示 DC
			spi_gpio_write_9bit(mosi_pin, clk_pin, cmd_with_dc);
		}
		for (size_t i = 0; i < data_len; i++) {
			uint16_t data_with_dc = data[i];
			spi_gpio_write_9bit(mosi_pin, clk_pin, data_with_dc);
		}
		break;
	}

	case YEPD_IF_SPI8DC: {
		ESP_LOGI(TAG, "YEPD_IF_SPI8DC");

		// 写入命令 (DC = 0)
		YEPD_GPIO_SET(dc_pin, 0);
		spi_gpio_write_buffer(mosi_pin, clk_pin, cmd, cmd_len);

		// 写入数据 (DC = 1)
		if (data && data_len > 0) {
			YEPD_GPIO_SET(dc_pin, 1);
			spi_gpio_write_buffer(mosi_pin, clk_pin, data,
					      data_len);
		}
		break;
	}

	case YEPD_IF_SPI8S: {
		ESP_LOGI(TAG, "YEPD_IF_SPI8S");

		// 直接写入命令和数据
		spi_gpio_write_buffer(mosi_pin, clk_pin, cmd, cmd_len);
		if (data && data_len > 0) {
			spi_gpio_write_buffer(mosi_pin, clk_pin, data,
					      data_len);
		}
		break;
	}

	case YEPD_IF_SPIQ: {
		ESP_LOGE(TAG,
			 "YEPD_IF_SPIQ is not supported in GPIO-based SPI");
		break;
	}

	default:
		ESP_LOGE(TAG, "Unsupported SPI interface: %d", epd->interface);
		break;
	}
}

/**
 * @brief 
 * 
 * @param epd 
 * @param sequence 
 */
void yepd_execute_sequence(YEPD *epd, const char *sequence)
{
	char line[128]; // 缓冲区用于存储一行命令
	const char *ptr = sequence; // 遍历字符串
	uint8_t cmd_buffer[64]; // 用于存储解析后的命令和数据
	uint8_t data_buffer[64]; // 用于存储数据部分

	while (*ptr) {
		// 提取一行
		const char *newline = strchr(ptr, '\n');
		size_t line_len = newline ? (newline - ptr) : strlen(ptr);
		strncpy(line, ptr, line_len);
		line[line_len] = '\0';
		ptr += line_len + (newline ? 1 : 0);

		// 解析 CS 掩码、忙检查、延迟、命令和数据
		char *token = strtok(line, " ");
		if (!token)
			continue;

		// 解析 CS 掩码
		int cs_mask = strtol(token, NULL, 16);
		if (cs_mask < 0 || cs_mask > 0xFF) {
			ESP_LOGE(TAG, "Invalid CS mask: %02X", cs_mask);
			continue;
		}

		// 解析忙检查
		token = strtok(NULL, " ");
		if (!token) {
			ESP_LOGE(TAG, "Invalid sequence: missing busy check");
			continue;
		}
		int busy_check = strtol(token, NULL, 16);

		// 解析延迟
		token = strtok(NULL, " ");
		if (!token) {
			ESP_LOGE(TAG, "Invalid sequence: missing delay");
			continue;
		}
		int delay_ms = strtol(token, NULL, 16);

		// 解析命令
		token = strtok(NULL, " ");
		if (!token) {
			ESP_LOGE(TAG, "Invalid sequence: missing command");
			continue;
		}
		cmd_buffer[0] = strtol(token, NULL, 16);

		// 解析数据
		size_t data_len = 0;
		while ((token = strtok(NULL, " ")) != NULL) {
			if (data_len >= sizeof(data_buffer)) {
				ESP_LOGE(TAG, "Data buffer overflow");
				break;
			}
			data_buffer[data_len++] = strtol(token, NULL, 16);
		}

		// 设置 CS 引脚低电平（激活）
		for (int i = 0; i < 8; i++) {
			if (cs_mask & (1 << i)) {
				if (epd->pin_cs[i] != -1) {
					gpio_set_level(epd->pin_cs[i], 0);
				}
			}
		}

		// 使用 yepd_write 发送命令和数据
		yepd_write(epd, cmd_buffer, 1, data_buffer, data_len);

		// 恢复 CS 引脚高电平（释放）
		for (int i = 0; i < 8; i++) {
			if (cs_mask & (1 << i)) {
				if (epd->pin_cs[i] != -1) {
					gpio_set_level(epd->pin_cs[i], 1);
				}
			}
		}

		// 检查忙引脚状态
		if (busy_check == 1) {
			while (!gpio_get_level(epd->pin_busy)) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		} else if (busy_check == 2) {
			while (gpio_get_level(epd->pin_busy)) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		}

		// 延迟处理
		if (delay_ms > 0) {
			vTaskDelay(pdMS_TO_TICKS(delay_ms));
		}
	}
}
