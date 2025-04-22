#include "arduino_wrapper.h"

static const char TAG[] = "Arduino-Wrapper";
#if 1 // Serial
static void _serial_begin(int baud)
{
	/*uart_config_t uart_config = { .baud_rate = baud,
				      .data_bits = UART_DATA_8_BITS,
				      .parity = UART_PARITY_DISABLE,
				      .stop_bits = UART_STOP_BITS_1,
				      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
	uart_param_config(UART_PORT, &uart_config);
	uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);*/
}

static int _serial_printf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int len = vprintf(fmt, args);
	va_end(args);
	return len;
}

static void _serial_println(const char *str)
{
	printf("%s\n", str);
}

struct AW_Serial Serial = { .begin = _serial_begin,
			    .printf = _serial_printf,
			    .println = _serial_println };
#endif // Serial

#if 1 // GPIO
static void _pin_mode(int pin, AW_PinMode mode)
{
	gpio_config_t io_conf = { .pin_bit_mask = (1ULL << pin),
				  .intr_type = GPIO_INTR_DISABLE,
				  .mode = GPIO_MODE_INPUT,
				  .pull_up_en = GPIO_PULLUP_DISABLE,
				  .pull_down_en = GPIO_PULLDOWN_DISABLE };

	switch (mode) {
	case OUTPUT:
		io_conf.mode = GPIO_MODE_OUTPUT;
		break;
	case OUTPUT_OPEN_DRAIN:
		io_conf.mode = GPIO_MODE_OUTPUT_OD;
		break;
	case INPUT_PULLUP:
		io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
		break;
	case INPUT_PULLDOWN:
		io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
		break;
	default: // INPUT
		break;
	}

	ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void _digital_write(int pin, AW_GPIO_Level level)
{
	gpio_set_level(pin, level);
}

static int _digital_read(int pin)
{
	return gpio_get_level(pin);
}

AW_GPIO Digital = { .pinMode = _pin_mode,
		    .digitalWrite = _digital_write,
		    .digitalRead = _digital_read };

void pinMode(int pin, AW_PinMode mode)
{
	Digital.pinMode(pin, mode);
}

void digitalWrite(int pin, AW_GPIO_Level level)
{
	Digital.digitalWrite(pin, level);
}

int digitalRead(int pin)
{
	return Digital.digitalRead(pin);
}

#endif // GPIO

#if 1 // I2C
#include "driver/i2c.h"

#define I2C_PORT I2C_NUM_0
static uint8_t i2c_address = 0;
static i2c_cmd_handle_t cmd = NULL;

static void _i2c_begin(int sda, int scl, uint32_t freq)
{
	i2c_config_t conf = { .mode = I2C_MODE_MASTER,
			      .sda_io_num = sda,
			      .scl_io_num = scl,
			      .sda_pullup_en = GPIO_PULLUP_ENABLE,
			      .scl_pullup_en = GPIO_PULLUP_ENABLE,
			      .master.clk_speed = freq };
	i2c_param_config(I2C_PORT, &conf);
	i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static void _i2c_begin_transmission(uint8_t addr)
{
	//ESP_LOGE(TAG, "[I2C] Start transmission to 0x%02X\n", addr); // 地址打印
	i2c_address = addr;
	cmd = i2c_cmd_link_create();
	if (!cmd) {
		ESP_LOGE(
			TAG,
			"[I2C] Error creating command link!\n"); // 命令链创建检查
		return;
	}
	esp_err_t ret = i2c_master_start(cmd);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[I2C] Error adding start condition: 0x%X\n",
			 ret);
	}
	ret = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[I2C] Error writing address byte: 0x%X\n", ret);
	}
}

static size_t _i2c_write(const uint8_t *data, size_t len)
{
	esp_err_t ret = i2c_master_write(cmd, data, len, true);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[I2C] Error data");
	}
	return len;
}

static int _i2c_end_transmission(void)
{
	if (!cmd) {
		ESP_LOGE(TAG, "[I2C] No active command link!\n");
		return -1;
	}

	// 添加停止条件
	esp_err_t ret = i2c_master_stop(cmd);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[I2C] Error adding stop condition: 0x%X\n", ret);
		i2c_cmd_link_delete(cmd);
		return -1;
	}

	// 执行传输
	ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
	i2c_cmd_link_delete(cmd);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[I2C] Transmission failed: %s (0x%X)\n",
			 esp_err_to_name(ret), ret); // 打印详细错误
		return -1;
	}

	//ESP_LOGE(TAG, "[I2C] Transmission success\n");
	return 0;
}

static void _end(void)
{
	//TODO

	// 删除驱动
	esp_err_t ret = i2c_driver_delete(I2C_PORT);
	if (ret != ESP_OK) {
		// 处理错误
		ESP_LOGE("I2C", "Failed to delete driver: %s",
			 esp_err_to_name(ret));
	}
}

AW_I2C Wire = {
	.begin = _i2c_begin,
	.beginTransmission = _i2c_begin_transmission,
	.write = _i2c_write,
	.endTransmission = _i2c_end_transmission,
	.end = _end,
};

void check_i2c_devices(void)
{
	ESP_LOGE(TAG, "Scanning I2C bus...\n");
	for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE,
				      true);
		i2c_master_stop(cmd);

		esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, 50);
		i2c_cmd_link_delete(cmd);

		if (ret == ESP_OK) {
			ESP_LOGE(TAG, "Found device at 0x%02X\n", addr);
		}
	}
}

#endif // I2C

#if 1 //SPI

#define SPI_HOST SPI2_HOST
static spi_device_handle_t spi_device = NULL;
static uint8_t current_mode = 0;
static uint32_t current_clock = 1000000;
static bool use_hw_cs = true; // 默认启用硬件CS

static void _spi_begin(int sclk, int miso, int mosi, int ss)
{
	spi_bus_config_t buscfg = { .mosi_io_num = mosi,
				    .miso_io_num = miso,
				    .sclk_io_num = sclk,
				    .quadwp_io_num = -1,
				    .quadhd_io_num = -1,
				    .max_transfer_sz = 4096 };

	// 初始化总线（仅需一次）
	static bool bus_initialized = false;
	if (!bus_initialized) {
		ESP_ERROR_CHECK(
			spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
		bus_initialized = true;
	}

	// 添加设备
	//spi_device_interface_config_t devcfg = { .mode = 0,
	//					 .clock_speed_hz = 1000000,
	//					 .spics_io_num = ss };
	//ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &devcfg, &spi_device));
}

static void _spi_end(void)
{
	// 1. 移除设备
	if (spi_device != NULL) {
		esp_err_t ret = spi_bus_remove_device(spi_device);
		if (ret != ESP_OK) {
			ESP_LOGE("SPI", "Remove device failed: %s",
				 esp_err_to_name(ret));
		} else {
			spi_device = NULL; // 清除句柄
			ESP_LOGI("SPI", "Device removed");
		}
	}

	// 2. 释放总线（仅在确定不再使用SPI时调用）
	// spi_bus_free(SPI_HOST); // 暂时注释掉此步骤
}

static void _spi_set_hw_cs(bool enable)
{
	use_hw_cs = enable;
	// 如果已存在设备，需要重新初始化
	if (spi_device) {
		ESP_LOGE(TAG, "spi device exist, it will be remove!");
		//spi_bus_remove_device(spi_device);
		//spi_device = NULL;
	}
}

static void _spi_begin_transaction(uint32_t clockSpeed, uint8_t bitOrder,
				   uint8_t dataMode)
{
	// 移除设备（如果已存在）
	if (spi_device) {
		spi_bus_remove_device(spi_device);
	}

	// 创建新的设备配置
	spi_device_interface_config_t devcfg = {
		.mode = dataMode,
		.clock_speed_hz = clockSpeed, // 时钟频率
		.spics_io_num = -1, // 手动管理CS
		.queue_size = 1, // 事务队列长度
		.flags = (bitOrder == LSBFIRST) ? (SPI_DEVICE_TXBIT_LSBFIRST |
						   SPI_DEVICE_RXBIT_LSBFIRST) :
						  0,
		.input_delay_ns = 0,
	};

	// 添加新设备
	spi_bus_add_device(SPI_HOST, &devcfg, &spi_device);

	current_mode = dataMode;
	current_clock = clockSpeed;
}

static uint8_t _spi_transfer(uint8_t data)
{
	spi_transaction_t t = {
		.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
		.length = 8, // 8 bits
		.tx_data = { data }, // 使用tx_data字段
		.rx_data = { 0 } // 使用rx_data字段
	};

	if (spi_device_polling_transmit(spi_device, &t) != ESP_OK) {
		return 0xFF;
	}
	return t.rx_data[0]; // 从固定缓冲区获取数据
}

static void _spi_transfer_bytes(const uint8_t *tx, uint8_t *rx, size_t len)
{
	size_t max_chunk =
		SOC_SPI_MAXIMUM_BUFFER_SIZE; // 自动获取芯片支持的最大长度
	size_t transferred = 0;

	int chunk_cnt = 0;

	while (len > 0) {
		// 计算本次传输块大小
		size_t chunk = (len > max_chunk) ? max_chunk : len;

		// 配置传输描述符
		spi_transaction_t t = {
			.length = chunk * 8, // 计算总位数
			.tx_buffer = tx ? tx + transferred : NULL,
			.rx_buffer = rx ? rx + transferred : NULL
		};

		spi_device_transmit(spi_device, &t);

		// 更新计数器和指针偏移
		transferred += chunk;
		len -= chunk;

		//delay(10);
		if ((transferred % 1000) == 0) {
			printf(".");
			fflush(stdout);
		}

		chunk_cnt++;
	}

	if (chunk_cnt > 1) {
		printf("\n");
		fflush(stdout);
	}
}

static void _spi_end_transaction(void)
{
	// 可选的清理操作
}

AW_SPI epd_spi = {
	.begin = _spi_begin,
	.setHwCs = _spi_set_hw_cs,
	.beginTransaction = _spi_begin_transaction,
	.transfer = _spi_transfer,
	.transferBytes = _spi_transfer_bytes,
	.endTransaction = _spi_end_transaction,
	.end = _spi_end,
};

#endif //SPI