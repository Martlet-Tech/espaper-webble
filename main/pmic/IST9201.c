#include "IST9201.h"
#include <stdio.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { HW_VER_3_0 = 0, HW_VER_3_1, HW_VER_3_2, HW_VER_3_3 };

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

#define PMIC_ADDR 0x48
//寄存器偏移
#define SET_VPOS_VENG2 2
#define SET_VPOS_VENG3 3
#define SET_VCOMDC 4

#define TAG "IST9201"

int hw_version = HW_VER_3_3;

unsigned char initPmicData[28] = {
	//-----setting VPOS1 +15V
	0x02, //value1
	0xE3, //value2
	//-----setting VNEG1 -15V
	0x02, //value1
	0xE3, //value2
	//-----setting VPOS2 +10V
	0x01, //value1
	0xC7, //value2
	//-----setting VNEG2
	0x01, //value1
	0xC7, //value2
	//-----setting VPOS3
	0x00, //value1
	0x73, //value2
	//-----setting VNEG3
	0x00, //value1
	0x73, //value2
	//-----setting DC VCOM
	0x00, //value1
	0x9A, //value2
	//-----setting VCOMH
	0x11, //value1
	0x3F, //value2
	//-----setting VCOML
	0x00, //value1
	0x9F, //value2
	//-----setting Delay Time 1
	0xFF, //0xAA,  //value
	//-----setting Delay Time 2
	0xFF, //0xAA,  //value
	//-----setting VDDH_EXT delay time
	0x80, //value
	//-----setting VGH1
	0xD1, //0xD8,  //value1
	0x94, //0xBC,  //value2
	//-----setting VGH2
	0x11, //0x31,  //value
	0x94, //value
	//-----setting 0x1A (VPDD)
	0x00, //value
	//-----setting 0x1B
	0x80, //value
	//-----setting 0x1C
	0xA8 //value
};

#if 0
_IST9201(int pmic_en, int ist_en, int vddp_en, int vddn_en, int vncp_en,
	 int sda_pin, int scl_pin, int pg_pin, int ps_pin)
{
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = _sda_pin;
	conf.scl_io_num = _scl_pin;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 100000;

	// 配置I2C参数并安装驱动
	ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode,
					   I2C_MASTER_RX_BUF_DISABLE,
					   I2C_MASTER_TX_BUF_DISABLE, 0));
}
#endif

//Macros
#define ERROR 1
#define DONE 0
//3.3
static const int PMIC_EN_PIN = 45;
static const int IST9201_EN_PIN = 21;
static const int IST9201_SDA_PIN = 39;
static const int IST9201_SCL_PIN = 38;
#define IST9201_PG_PIN 41
#define IST9201_PS_PIN 40
#define VDDP_EN_PIN 1 //+19V DCDC的EN脚
#define VDDN_EN_PIN 2 //-19V DCDC的EN脚
#define VNCP_EN_PIN 42 //-3.5V DCDC的EN脚

typedef enum {
	IST9201_PM_INPUT = 0,
	IST9201_PM_OUTPUT,
	IST9201_PM_INPUT_PULLUP,
	IST9201_PM_INPUT_PULLDOWN,
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

static void ist9201_i2c_scan(void)
{
	ESP_LOGW(TAG, "Scanning I2C bus...");
	for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
		i2c_master_stop(cmd);
		esp_err_t ret =
			i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
		i2c_cmd_link_delete(cmd);
		if (ret == ESP_OK) {
			ESP_LOGW(TAG, "Found device at 0x%02X", addr);
		}
	}
}

static esp_err_t ist9201_i2c_write(uint8_t addr_7bit, const uint8_t *data,
				   size_t len)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (!cmd) {
		return ESP_ERR_NO_MEM;
	}
	esp_err_t ret = i2c_master_start(cmd);
	if (ret != ESP_OK) {
		goto out;
	}
	ret = i2c_master_write_byte(cmd, (addr_7bit << 1) | I2C_MASTER_WRITE,
				    true);
	if (ret != ESP_OK) {
		goto out;
	}
	ret = i2c_master_write(cmd, data, len, true);
	if (ret != ESP_OK) {
		goto out;
	}
	ret = i2c_master_stop(cmd);
	if (ret != ESP_OK) {
		goto out;
	}
	ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
out:
	i2c_cmd_link_delete(cmd);
	return ret;
}

static void _powerSwitchDisable(void);
static void _powerSwitchEnable(void);
static unsigned char _sendPmicData(unsigned char *dataBuffer,
				   unsigned int dataLength);
static void _PowerOnPMIC(void);
static void _PowerOffPMIC(void);
static void _DelayMs(unsigned int mstime);

static void _DetectHWVersion(void)
{
	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLDOWN);
	if (ist9201_digital_read(IST9201_PG_PIN) == 0) {
		ESP_LOGI(TAG, "HW Version = V3.3");
		hw_version = HW_VER_3_3;
	} else {
		ESP_LOGI(TAG, "HW Version < V3.3");
		hw_version = HW_VER_3_2;
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

	ESP_LOGI(TAG, "IST9201 IO Initialized.");

	ist9201_pin_mode(IST9201_SDA_PIN, IST9201_PM_OUTPUT);
	ist9201_pin_mode(IST9201_SCL_PIN, IST9201_PM_OUTPUT);
	ist9201_digital_write(IST9201_SDA_PIN, 0);
	ist9201_digital_write(IST9201_SCL_PIN, 0);
}

static void _IfInit(void)
{
	_PowerOnPMIC();
	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLUP);

	i2c_config_t conf = { .mode = I2C_MODE_MASTER,
			      .sda_io_num = IST9201_SDA_PIN,
			      .scl_io_num = IST9201_SCL_PIN,
			      .sda_pullup_en = GPIO_PULLUP_ENABLE,
			      .scl_pullup_en = GPIO_PULLUP_ENABLE,
			      .master.clk_speed = 100000 };
	ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
	ESP_LOGI(TAG, "IST9201 I2C Interface Initialized.");
}

static void _IfDeinit(void)
{
	esp_err_t ret = i2c_driver_delete(I2C_MASTER_NUM);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "i2c_driver_delete failed: %s",
			 esp_err_to_name(ret));
	}
	ist9201_pin_mode(IST9201_PG_PIN, IST9201_PM_INPUT_PULLDOWN);
	ESP_LOGI(TAG, "IST9201 I2C Interface Deunitialized.");
	_PowerOffPMIC();
}

static void _PowerOnPMIC(void)
{
	ESP_LOGI(TAG, "Power On IST9201 & EPD");
	ist9201_digital_write(PMIC_EN_PIN,
			      1); //Power On the IST9201 & EPD(AVDD\DVDD\1.35V LDO)
	ist9201_digital_write(IST9201_PS_PIN, 1);
	_DelayMs(200);
}

static void _PowerOffPMIC(void)
{
	ESP_LOGI(TAG, "Power Off IST9201 & EPD.");
	_DelayMs(1000);
	ist9201_digital_write(PMIC_EN_PIN,
			      0); //Power Off the IST9201 & EPD(AVDD\DVDD\1.35V LDO)
	_DelayMs(1);
	ist9201_digital_write(IST9201_PS_PIN, 0); //get rid off the leak current.
	_DelayMs(200);
}

static unsigned char _setPmic(void)
{
	unsigned char status;
	unsigned char buf[3];

	buf[0] = 0x01;
	buf[1] = initPmicData[0];
	buf[2] = initPmicData[1];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x05;
	buf[1] = initPmicData[4];
	buf[2] = initPmicData[5];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x09;
	buf[1] = initPmicData[8];
	buf[2] = initPmicData[9];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x03;
	buf[1] = initPmicData[2];
	buf[2] = initPmicData[3];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x07;
	buf[1] = initPmicData[6];
	buf[2] = initPmicData[7];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x0B;
	buf[1] = initPmicData[10];
	buf[2] = initPmicData[11];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x0D;
	buf[1] = initPmicData[12];
	buf[2] = initPmicData[13];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x13;
	buf[1] = initPmicData[18];
	status = _sendPmicData(buf, 2);

	buf[0] = 0x14;
	buf[1] = initPmicData[19];
	status = _sendPmicData(buf, 2);

	buf[0] = 0x15;
	buf[1] = initPmicData[20];
	status = _sendPmicData(buf, 2);

	buf[0] = 0x16;
	buf[1] = initPmicData[21];
	buf[2] = initPmicData[22];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x18;
	buf[1] = initPmicData[23];
	buf[2] = initPmicData[24];
	status = _sendPmicData(buf, 3);

	buf[0] = 0x1A;
	buf[1] = initPmicData[25];
	status = _sendPmicData(buf, 2);

	buf[0] = 0x1B;
	buf[1] = initPmicData[26];
	ESP_LOGI(TAG, "1. 0x%02X, 2. 0x%02X", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	buf[0] = 0x1C;
	buf[1] = initPmicData[27];
	ESP_LOGI(TAG, "1. 0x%02X, 2. 0x%02X", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	return status;
}

static void _DelayMs(unsigned int mstime)
{
	ist9201_delay_ms(mstime);
}

static unsigned char _sendPmicData(unsigned char *dataBuffer,
				   unsigned int dataLength)
{
	if (ist9201_i2c_write(PMIC_ADDR, dataBuffer, dataLength) == ESP_OK) {
		return DONE;
	}
	ESP_LOGE(TAG, "Error sending I2C data.");
	return ERROR;
}

static void _enablePmic(void)
{
	ist9201_digital_write(IST9201_PS_PIN, 0); //IST9201 leave power save mode
	_DelayMs(50);
	if (_setPmic() == DONE) {
		_powerSwitchEnable();
		ist9201_digital_write(IST9201_EN_PIN, 1); //Power on rails
		while (!ist9201_digital_read(IST9201_PG_PIN)) {
			ist9201_delay_ms(10);
		}
		ESP_LOGI(TAG, "enablePmic() done.");
	} else {
		ist9201_i2c_scan();
		ESP_LOGE(TAG, "enablePmic() error.");
	}
}

static void _disablePmic(void)
{
	ESP_LOGI(TAG, "Power Off Sequence Start ...");
	ist9201_digital_write(IST9201_EN_PIN, 0); //Power off rails
	while (ist9201_digital_read(IST9201_PG_PIN))
		;
	ESP_LOGI(TAG, "IST9201 PG Low.");
	if (hw_version < HW_VER_3_3)
		_DelayMs(5000);
	else
		_DelayMs(1000);
	ist9201_digital_write(IST9201_PS_PIN, 1); //IST9201 enter power save mode
	_powerSwitchDisable();
}

static void _powerSwitchEnable(void)
{
	ist9201_digital_write(VDDN_EN_PIN, 1);
	_DelayMs(20);
	ist9201_digital_write(VDDP_EN_PIN, 1);
	_DelayMs(20);
	ist9201_digital_write(VNCP_EN_PIN, 1);
	_DelayMs(20);
}

static void _powerSwitchDisable(void)
{
	ESP_LOGI(TAG, "Power Switch Disable Start.");
	ist9201_digital_write(VNCP_EN_PIN, 0);
	_DelayMs(300);
	ist9201_digital_write(VDDP_EN_PIN, 0);
	if (hw_version < HW_VER_3_3)
		_DelayMs(2000);
	else
		_DelayMs(300);
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
