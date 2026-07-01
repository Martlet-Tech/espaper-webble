#include "IST9201.h"
#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { HW_VER_3_0 = 0, HW_VER_3_1, HW_VER_3_2, HW_VER_3_3 };

#define ERROR 1
#define DONE 0

#define I2C_MASTER_TIMEOUT_MS 1000

#define PMIC_ADDR 0x48
#define IST9201_PG_PIN 41
#define IST9201_PS_PIN 40
#define VDDP_EN_PIN 1
#define VDDN_EN_PIN 2
#define VNCP_EN_PIN 42

#define SET_VPOS_VENG2 2
#define SET_VPOS_VENG3 3
#define SET_VCOMDC 4

#define TAG "IST9201"

int hw_version = HW_VER_3_3;

unsigned char initPmicData[28] = {
	0x02, 0xE3,
	0x02, 0xE3,
	0x01, 0xC7,
	0x01, 0xC7,
	0x00, 0x73,
	0x00, 0x73,
	0x00, 0x9A,
	0x11, 0x3F,
	0x00, 0x9F,
	0xFF,
	0xFF,
	0x80,
	0xD1, 0x94,
	0x11, 0x94,
	0x00,
	0x80,
	0xA8
};

static int PMIC_EN_PIN;
static int IST9201_EN_PIN;
static int IST9201_SDA_PIN;
static int IST9201_SCL_PIN;

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

typedef enum {
	IST9201_PM_INPUT = 0,
	IST9201_PM_OUTPUT,
	IST9201_PM_INPUT_PULLUP,
	IST9201_PM_INPUT_PULLDOWN,
	IST9201_PM_OUTPUT_OD,
} ist9201_pm_t;

static void ist9201_pin_mode(int pin, ist9201_pm_t m)
{
	gpio_mode_t mode = GPIO_MODE_INPUT;
	gpio_pullup_t pu = GPIO_PULLUP_DISABLE;
	gpio_pulldown_t pd = GPIO_PULLDOWN_DISABLE;

	switch (m) {
	case IST9201_PM_OUTPUT:
		mode = GPIO_MODE_OUTPUT;
		break;
	case IST9201_PM_INPUT_PULLUP:
		mode = GPIO_MODE_INPUT;
		pu = GPIO_PULLUP_ENABLE;
		break;
	case IST9201_PM_INPUT_PULLDOWN:
		mode = GPIO_MODE_INPUT;
		pd = GPIO_PULLDOWN_ENABLE;
		break;
	case IST9201_PM_OUTPUT_OD:
		mode = GPIO_MODE_INPUT_OUTPUT_OD;
		pu = GPIO_PULLUP_ENABLE;
		break;
	default:
		break;
	}

	gpio_config_t io = { .pin_bit_mask = 1ULL << pin,
			     .mode = mode,
			     .pull_up_en = pu,
			     .pull_down_en = pd,
			     .intr_type = GPIO_INTR_DISABLE };
	ESP_ERROR_CHECK(gpio_config(&io));
}

static void ist9201_digital_write(int pin, int level)
{
	gpio_set_level(pin, level);
}

static int ist9201_digital_read(int pin)
{
	return gpio_get_level(pin);
}

static void ist9201_delay_ms(unsigned int ms)
{
	if (ms == 0) {
		return;
	}
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static esp_err_t i2c_probe_addr(uint8_t addr)
{
	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = addr,
		.scl_speed_hz = 100000,
	};
	i2c_master_dev_handle_t dev;
	esp_err_t ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev);
	if (ret != ESP_OK) {
		return ret;
	}
	uint8_t dummy = 0;
	ret = i2c_master_transmit(dev, &dummy, 1, 50);
	i2c_master_bus_rm_device(dev);
	return ret;
}

static void ist9201_i2c_scan(void)
{
	ESP_LOGW(TAG, "Scanning I2C bus...");
	for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
		esp_err_t ret = i2c_probe_addr(addr);
		if (ret == ESP_OK) {
			ESP_LOGW(TAG, "Found device at 0x%02X", addr);
		}
	}
}

static void _DelayMs(unsigned int mstime)
{
	ist9201_delay_ms(mstime);
}

static void _powerSwitchDisable(void);
static void _powerSwitchEnable(void);

static unsigned char _sendPmicData(unsigned char *dataBuffer,
				   unsigned int dataLength)
{
	if (s_dev_handle == NULL) {
		ESP_LOGE(TAG, "I2C device not initialized");
		return ERROR;
	}
	esp_err_t ret = i2c_master_transmit(s_dev_handle, dataBuffer,
					    dataLength, I2C_MASTER_TIMEOUT_MS);
	if (ret == ESP_OK) {
		return DONE;
	}
	ESP_LOGE(TAG, "I2C write error: %s (addr=0x%02X, len=%d)",
		 esp_err_to_name(ret), PMIC_ADDR, dataLength);
	return ERROR;
}

static unsigned char _setPmic(void)
{
	unsigned char status;
	unsigned char buf[3];

	ESP_LOGI(TAG, "_setPmic: writing 15 register pairs...");

	buf[0] = 0x01; buf[1] = initPmicData[0]; buf[2] = initPmicData[1];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x01: status=%d", status);

	buf[0] = 0x05; buf[1] = initPmicData[4]; buf[2] = initPmicData[5];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x05: status=%d", status);

	buf[0] = 0x09; buf[1] = initPmicData[8]; buf[2] = initPmicData[9];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x09: status=%d", status);

	buf[0] = 0x03; buf[1] = initPmicData[2]; buf[2] = initPmicData[3];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x03: status=%d", status);

	buf[0] = 0x07; buf[1] = initPmicData[6]; buf[2] = initPmicData[7];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x07: status=%d", status);

	buf[0] = 0x0B; buf[1] = initPmicData[10]; buf[2] = initPmicData[11];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x0B: status=%d", status);

	buf[0] = 0x0D; buf[1] = initPmicData[12]; buf[2] = initPmicData[13];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x0D: status=%d", status);

	buf[0] = 0x13; buf[1] = initPmicData[18];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x13: status=%d", status);

	buf[0] = 0x14; buf[1] = initPmicData[19];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x14: status=%d", status);

	buf[0] = 0x15; buf[1] = initPmicData[20];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x15: status=%d", status);

	buf[0] = 0x16; buf[1] = initPmicData[21]; buf[2] = initPmicData[22];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x16: status=%d", status);

	buf[0] = 0x18; buf[1] = initPmicData[23]; buf[2] = initPmicData[24];
	status = _sendPmicData(buf, 3);
	ESP_LOGD(TAG, "  reg 0x18: status=%d", status);

	buf[0] = 0x1A; buf[1] = initPmicData[25];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x1A: status=%d", status);

	buf[0] = 0x1B; buf[1] = initPmicData[26];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x1B: status=%d, data=0x%02X", status, buf[1]);

	buf[0] = 0x1C; buf[1] = initPmicData[27];
	status = _sendPmicData(buf, 2);
	ESP_LOGD(TAG, "  reg 0x1C: status=%d, data=0x%02X", status, buf[1]);

	return status;
}

static void _PowerOnPMIC(void)
{
	ESP_LOGI(TAG, "Power On IST9201 & EPD");
	ESP_LOGI(TAG, "  PMIC_EN_PIN=1, IST9201_PS_PIN=1");
	ist9201_digital_write(PMIC_EN_PIN, 1);
	ist9201_digital_write(IST9201_PS_PIN, 1);
	_DelayMs(200);
	ESP_LOGI(TAG, "  PowerOn done");
}

static void _PowerOffPMIC(void)
{
	ESP_LOGI(TAG, "Power Off IST9201 & EPD.");
	ist9201_digital_write(IST9201_EN_PIN, 0);
	int pg_wait = 0;
	while (ist9201_digital_read(IST9201_PG_PIN)) {
		ist9201_delay_ms(1);
		pg_wait++;
		if (pg_wait > 500) {
			ESP_LOGW(TAG, "  PG low wait timeout after %d ms", pg_wait);
			break;
		}
	}
	ESP_LOGI(TAG, "  PG low after %d ms", pg_wait);
	_powerSwitchDisable();
	_DelayMs(100);
	ESP_LOGI(TAG, "  PMIC_EN_PIN=0");
	ist9201_digital_write(PMIC_EN_PIN, 0);
	_DelayMs(1);
	ist9201_digital_write(IST9201_PS_PIN, 0);
	_DelayMs(200);
	ESP_LOGI(TAG, "  PowerOff done");
}

static void _DetectHWVersion(void)
{
	ESP_LOGI(TAG, "_DetectHWVersion: start");

	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLDOWN);
	int pg_val = ist9201_digital_read(IST9201_PG_PIN);

	if (pg_val == 0) {
		ESP_LOGI(TAG, "HW Version = V3.3");
		hw_version = HW_VER_3_3;
	} else {
		ESP_LOGI(TAG, "HW Version < V3.3");
		hw_version = HW_VER_3_2;
	}

	if (hw_version < HW_VER_3_3) {
		PMIC_EN_PIN = 21;
		IST9201_EN_PIN = 47;
		IST9201_SDA_PIN = 48;
		IST9201_SCL_PIN = 45;
		ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLUP);
	} else {
		PMIC_EN_PIN = 45;
		IST9201_EN_PIN = 21;
		IST9201_SDA_PIN = 39;
		IST9201_SCL_PIN = 38;
		ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT);
	}

	ist9201_pin_mode(PMIC_EN_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(VNCP_EN_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(VDDP_EN_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(VDDN_EN_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(IST9201_PS_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(IST9201_EN_PIN, IST9201_PM_OUTPUT);

	ist9201_digital_write(PMIC_EN_PIN, 0);
	ist9201_digital_write(VNCP_EN_PIN, 0);
	ist9201_digital_write(VDDP_EN_PIN, 0);
	ist9201_digital_write(VDDN_EN_PIN, 0);
	ist9201_digital_write(IST9201_PS_PIN, 0);
	ist9201_digital_write(IST9201_EN_PIN, 0);

	ist9201_pin_mode(IST9201_SDA_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(IST9201_SCL_PIN, IST9201_PM_OUTPUT);
	ist9201_digital_write(IST9201_SDA_PIN, 0);
	ist9201_digital_write(IST9201_SCL_PIN, 0);

	ESP_LOGI(TAG, "IST9201 IO Initialized.");
	ESP_LOGI(TAG, "_DetectHWVersion: done, hw_version=%d", hw_version);
}

static void _IfInit(void)
{
	ESP_LOGI(TAG, "_IfInit: start");

	_PowerOnPMIC();
	ESP_LOGI(TAG, "_IfInit: PowerOnPMIC done");

	ist9201_pin_mode(IST9201_SDA_PIN, IST9201_PM_OUTPUT_OD);
	ist9201_pin_mode(IST9201_SCL_PIN, IST9201_PM_OUTPUT_OD);
	ist9201_digital_write(IST9201_SDA_PIN, 1);
	ist9201_digital_write(IST9201_SCL_PIN, 1);
	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLUP);

	if (s_bus_handle != NULL) {
		ESP_LOGI(TAG, "_IfInit: bus already initialized, deleting first");
		if (s_dev_handle) {
			i2c_master_bus_rm_device(s_dev_handle);
			s_dev_handle = NULL;
		}
		i2c_del_master_bus(s_bus_handle);
		s_bus_handle = NULL;
	}

	i2c_master_bus_config_t bus_config = {
		.i2c_port = -1,
		.sda_io_num = IST9201_SDA_PIN,
		.scl_io_num = IST9201_SCL_PIN,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus_handle));

	i2c_device_config_t dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = PMIC_ADDR,
		.scl_speed_hz = 100000,
	};
	ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus_handle, &dev_config,
						  &s_dev_handle));

	ESP_LOGI(TAG, "IST9201 I2C Interface Initialized.");
	ist9201_i2c_scan();
	ESP_LOGI(TAG, "_IfInit: done");
}

static void _IfDeinit(void)
{
	if (s_dev_handle) {
		i2c_master_bus_rm_device(s_dev_handle);
		s_dev_handle = NULL;
	}
	if (s_bus_handle) {
		i2c_del_master_bus(s_bus_handle);
		s_bus_handle = NULL;
	}
	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLDOWN);
	ist9201_pin_mode(IST9201_SDA_PIN, IST9201_PM_OUTPUT_OD);
	ist9201_pin_mode(IST9201_SCL_PIN, IST9201_PM_OUTPUT_OD);
	ist9201_digital_write(IST9201_SDA_PIN, 0);
	ist9201_digital_write(IST9201_SCL_PIN, 0);
	ESP_LOGI(TAG, "IST9201 I2C Interface Deunitialized.");
	_PowerOffPMIC();
}

static void _enablePmic(void)
{
	ESP_LOGI(TAG, "_enablePmic: start");
	ist9201_digital_write(IST9201_PS_PIN, 0);
	_DelayMs(50);
	ESP_LOGI(TAG, "_enablePmic: PS_PIN=0, calling _setPmic");
	if (_setPmic() == DONE) {
		ESP_LOGI(TAG, "_enablePmic: _setPmic OK, powerSwitchEnable");
		_powerSwitchEnable();
		ESP_LOGI(TAG, "_enablePmic: set EN_PIN=1, waiting PG...");
		ist9201_digital_write(IST9201_EN_PIN, 1);
		int pg_wait = 0;
		while (!ist9201_digital_read(IST9201_PG_PIN)) {
			ist9201_delay_ms(10);
			pg_wait++;
			if (pg_wait > 500) {
				ESP_LOGE(TAG, "_enablePmic: PG timeout after %d ms!",
					 pg_wait * 10);
				break;
			}
		}
		ESP_LOGI(TAG, "_enablePmic: PG high after %d ms, done",
			 pg_wait * 10);
	} else {
		ist9201_i2c_scan();
		ESP_LOGE(TAG, "_enablePmic: _setPmic failed!");
	}
}

static void _disablePmic(void)
{
	ESP_LOGI(TAG, "Power Off Sequence Start ...");
	ist9201_digital_write(IST9201_EN_PIN, 0);
	int pg_wait = 0;
	while (ist9201_digital_read(IST9201_PG_PIN)) {
		ist9201_delay_ms(1);
		pg_wait++;
		if (pg_wait > 5000) {
			ESP_LOGW(TAG, "_disablePmic: PG wait timeout");
			break;
		}
	}
	ESP_LOGI(TAG, "IST9201 PG Low after %d ms.", pg_wait);
	if (hw_version < HW_VER_3_3)
		_DelayMs(5000);
	else
		_DelayMs(1000);
	ist9201_digital_write(IST9201_PS_PIN, 1);
	_powerSwitchDisable();
}

static void _powerSwitchEnable(void)
{
	ESP_LOGI(TAG, "_powerSwitchEnable: VDDN=1");
	ist9201_digital_write(VDDN_EN_PIN, 1);
	_DelayMs(20);
	ESP_LOGI(TAG, "_powerSwitchEnable: VDDP=1");
	ist9201_digital_write(VDDP_EN_PIN, 1);
	_DelayMs(20);
	ESP_LOGI(TAG, "_powerSwitchEnable: VNCP=1");
	ist9201_digital_write(VNCP_EN_PIN, 1);
	_DelayMs(20);
}

static void _powerSwitchDisable(void)
{
	ESP_LOGI(TAG, "Power Switch Disable Start.");
	ESP_LOGI(TAG, "  VNCP=0");
	ist9201_digital_write(VNCP_EN_PIN, 0);
	_DelayMs(300);
	ESP_LOGI(TAG, "  VDDP=0 (hw_ver=%d)", hw_version);
	ist9201_digital_write(VDDP_EN_PIN, 0);
	if (hw_version < HW_VER_3_3)
		_DelayMs(2000);
	else
		_DelayMs(300);
	ESP_LOGI(TAG, "  VDDN=0");
	ist9201_digital_write(VDDN_EN_PIN, 0);
	ESP_LOGI(TAG, "Power Switch Disable Done.");
}

static unsigned int _voltageToRegisterData(unsigned int voltageData,
					   unsigned char setSelect)
{
	unsigned int registerBuf = 0;
	switch (setSelect) {
	case 2:
		registerBuf = 1023 - (170 - voltageData) * 1000 / 176;
		break;
	case 3:
		registerBuf = 1023 - (270 - voltageData) * 1000 / 274;
		break;
	case 4:
		registerBuf = 255 - (50 - voltageData) * 250 / 49;
		break;
	default:
		return 0;
	}
	return registerBuf;
}

IST9201 ist9201 = {
	.setPmic = _setPmic,
	.sendPmicData = _sendPmicData,
	.DelayMs = _DelayMs,
	.DetectHWVersion = _DetectHWVersion,
	.IfInit = _IfInit,
	.IfDeinit = _IfDeinit,
	.PowerOnPMIC = _PowerOnPMIC,
	.PowerOffPMIC = _PowerOffPMIC,
	.enablePmic = _enablePmic,
	.disablePmic = _disablePmic,
	.powerSwitchEnable = _powerSwitchEnable,
	.powerSwitchDisable = _powerSwitchDisable,
	.voltageToRegisterData = _voltageToRegisterData,
};
