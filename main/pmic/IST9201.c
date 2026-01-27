#include "IST9201.h"
#include <stdio.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "arduino_wrapper.h"

enum { HW_VER_3_0 = 0, HW_VER_3_1, HW_VER_3_2, HW_VER_3_3 };

#define I2C_MASTER_NUM I2C_NUM_0 // I2C端口号
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

//ist9201.DetectHWVersion(); //TODO
//static const int _pmic_en = 45;
//static const int _ist_en = 21;
//static const int _vddp_en = 1;
//static const int _vddn_en = 2;
//static const int _vncp_en = 42;
//static const int _sda_pin = 39;
//static const int _scl_pin = 38;
//static const int _pg_pin = 41;
//static const int _ps_pin = 40;

//Macros
#define ERROR 1
#define DONE 0
//3.2
//static const int PMIC_EN_PIN = 21;
//static const int IST9201_EN_PIN = 47;
//static const int IST9201_SDA_PIN = 39;
//static const int IST9201_SCL_PIN = 45;
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

static void _powerSwitchDisable(void);
static void _powerSwitchEnable(void);
static unsigned char _sendPmicData(unsigned char *dataBuffer,
				   unsigned int dataLength);
static void _PowerOnPMIC(void);
static void _PowerOffPMIC(void);
static void _DelayMs(unsigned int mstime);

static void _DetectHWVersion(void)
{
	pinMode(IST9201_PG_PIN, INPUT_PULLDOWN);
	if (digitalRead(IST9201_PG_PIN) == 0) {
		Serial.println("HW Version = V3.3");
		hw_version = HW_VER_3_3;
	} else {
		Serial.println("HW Version < V3.3");
		hw_version = HW_VER_3_2;
	}

	//!!if (hw_version < HW_VER_3_3) {
	//!!	PMIC_EN_PIN = 21;
	//!!	IST9201_EN_PIN = 47;
	//!!	IST9201_SDA_PIN = 48;
	//!!	IST9201_SCL_PIN = 45;
	//!!	pinMode(IST9201_PG_PIN, INPUT_PULLUP);
	//!!} else {
	//!!	PMIC_EN_PIN = 45;
	//!!	IST9201_EN_PIN = 21;
	//!!	IST9201_SDA_PIN = 39;
	//!!	IST9201_SCL_PIN = 38;
	//!!	pinMode(IST9201_PG_PIN, INPUT);
	//!!}

	pinMode(PMIC_EN_PIN, OUTPUT);
	pinMode(VNCP_EN_PIN, OUTPUT);
	pinMode(VDDP_EN_PIN, OUTPUT);
	pinMode(VDDN_EN_PIN, OUTPUT);
	pinMode(IST9201_PS_PIN, OUTPUT);
	pinMode(IST9201_EN_PIN, OUTPUT);

	digitalWrite(PMIC_EN_PIN, LOW);
	digitalWrite(VNCP_EN_PIN, LOW);
	digitalWrite(VDDP_EN_PIN, LOW);
	digitalWrite(VDDN_EN_PIN, LOW);
	digitalWrite(IST9201_PS_PIN, LOW);
	digitalWrite(IST9201_EN_PIN, LOW);

	Serial.println("IST9201 IO Initialized.");

	pinMode(IST9201_SDA_PIN, OUTPUT);
	pinMode(IST9201_SCL_PIN, OUTPUT);
	digitalWrite(IST9201_SDA_PIN, LOW);
	digitalWrite(IST9201_SCL_PIN, LOW);
}

static void _IfInit(void)
{
	_PowerOnPMIC();
	//!!pinMode(IST9201_SDA_PIN, OUTPUT_OPEN_DRAIN);
	//!!pinMode(IST9201_SCL_PIN, OUTPUT_OPEN_DRAIN);
	//!!digitalWrite(IST9201_SDA_PIN, HIGH);
	//!!digitalWrite(IST9201_SCL_PIN, HIGH);
	pinMode(IST9201_PG_PIN, INPUT_PULLUP);
	Wire.begin(IST9201_SDA_PIN, IST9201_SCL_PIN, 100000);
	Serial.println("IST9201 I2C Interface Initialized.");
}

static void _IfDeinit()
{
	Wire.end();
	pinMode(IST9201_PG_PIN, INPUT_PULLDOWN);
	//!!pinMode(IST9201_SDA_PIN, OUTPUT_OPEN_DRAIN);
	//!!pinMode(IST9201_SCL_PIN, OUTPUT_OPEN_DRAIN);
	//!!digitalWrite(IST9201_SDA_PIN, LOW);
	//!!digitalWrite(IST9201_SCL_PIN, LOW);
	Serial.println("IST9201 I2C Interface Deunitialized.");
	_PowerOffPMIC();
}

static void _PowerOnPMIC(void)
{
	Serial.println("Power On IST9201 & EPD");
	digitalWrite(PMIC_EN_PIN,
		     HIGH); //Power On the IST9201 & EPD(AVDD\DVDD\1.35V LDO)
	digitalWrite(IST9201_PS_PIN, HIGH);
	_DelayMs(200);
}

static void _PowerOffPMIC(void)
{
	Serial.println("Power Off IST9201 & EPD.");
	//Make sure all power rails are completely shutdown first
	_DelayMs(1000);
	digitalWrite(PMIC_EN_PIN,
		     LOW); //Power Off the IST9201 & EPD(AVDD\DVDD\1.35V LDO)
	_DelayMs(1);
	digitalWrite(IST9201_PS_PIN, LOW); //get rid off the leak current.
	_DelayMs(200);
}

static unsigned char _setPmic(void)
{
	unsigned char status;
	unsigned char buf[3];

	//-----setting VPOS1
	buf[0] = 0x01; //register
	buf[1] = initPmicData[0]; //value1
	buf[2] = initPmicData[1]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VPOS2
	buf[0] = 0x05; //register
	buf[1] = initPmicData[4]; //value1
	buf[2] = initPmicData[5]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VPOS3
	buf[0] = 0x09; //register
	buf[1] = initPmicData[8]; //value1
	buf[2] = initPmicData[9]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VNEG1
	buf[0] = 0x03; //register
	buf[1] = initPmicData[2]; //value1
	buf[2] = initPmicData[3]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VNEG2
	buf[0] = 0x07; //register
	buf[1] = initPmicData[6]; //value1
	buf[2] = initPmicData[7]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VNEG3
	buf[0] = 0x0B; //register
	buf[1] = initPmicData[10]; //value1
	buf[2] = initPmicData[11]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting DC VCOM
	buf[0] = 0x0D; //register
	buf[1] = initPmicData[12]; //value1
	buf[2] = initPmicData[13]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//0x0F, 0x10, 0x11, 0x12   ΤU AVCOMH  VCOML b31.5 Τ   A ۷   AC VCOM

	//-----setting Delay Time 1
	buf[0] = 0x13; //register
	buf[1] = initPmicData[18]; //value
	// Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	//-----setting Delay Time 2
	buf[0] = 0x14; //register
	buf[1] = initPmicData[19]; //value
	// Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	//-----setting VDDH_EXT DelayMs time
	buf[0] = 0x15; //register
	buf[1] = initPmicData[20]; //value
	// Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	//-----setting VGH1
	buf[0] = 0x16; //register
	buf[1] = initPmicData[21]; //value1
	buf[2] = initPmicData[22]; //value2
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting VGH2
	buf[0] = 0x18; //register
	buf[1] = initPmicData[23]; //value
	buf[2] = initPmicData[24]; //value
	// Serial.printf("1. 0x%02X, 2. 0x%02X, 3. 0x%02X \r\n", buf[0], buf[1], buf[2]);
	status = _sendPmicData(buf, 3);

	//-----setting 0x1A (VPDD)
	buf[0] = 0x1A; //register
	buf[1] = initPmicData[25]; //value
	// Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	//-----setting 0x1B
	buf[0] = 0x1B; //register
	buf[1] = initPmicData[26]; //value
	Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	//-----setting 0x1C
	buf[0] = 0x1C; //register
	buf[1] = initPmicData[27]; //value
	Serial.printf("1. 0x%02X, 2. 0x%02X \r\n", buf[0], buf[1]);
	status = _sendPmicData(buf, 2);

	return status;
}

static void _DelayMs(unsigned int mstime)
{
	delay(mstime);
}

static unsigned char _sendPmicData(unsigned char *dataBuffer,
				   unsigned int dataLength)
{
	unsigned char status;
	Wire.beginTransmission(PMIC_ADDR);
	Wire.write(dataBuffer, dataLength);

	//Serial.printf("Wire.write. %d Bytes\r\n", ret);

	if (Wire.endTransmission() == 0) {
		status = DONE;
		// Serial.printf("Complete I2C data sending. ret = %d\r\n", ret);
	} else {
		status = ERROR;
		Serial.printf("Error sending I2C data. \r\n");
	}
	return status;
}

static void _enablePmic(void)
{
	digitalWrite(IST9201_PS_PIN, LOW); //IST9201 leave power save mode
	_DelayMs(50);
	if (_setPmic() == DONE) {
		_powerSwitchEnable();
		digitalWrite(IST9201_EN_PIN, HIGH); //Power on rails
		while (!(digitalRead(IST9201_PG_PIN))) {
			//Waiting for the PMIC_PG is the high level.
			delay(10);
		}
		Serial.printf("enablePmic() done. \r\n");
	} else {
		check_i2c_devices();
		Serial.printf("enablePmic() error. \r\n");
	}
}

static void _disablePmic(void)
{
	Serial.println("Power Off Sequence Start ...");
	digitalWrite(IST9201_EN_PIN, LOW); //Power off rails
	while (digitalRead(IST9201_PG_PIN))
		; //Waiting for the PMIC_PG is the low level.
	Serial.println("IST9201 PG Low.");
	//====== Please use DelayMs() to wait for PMIC's voltages to be 0V ======
	// Please confirm the discharge time of PMIC's power supply.
	if (hw_version < HW_VER_3_3)
		_DelayMs(5000); //OK-DEV03B V3.0~V3.2
	else
		_DelayMs(1000); //OK-DEV03B V3.3
	digitalWrite(IST9201_PS_PIN, HIGH); //IST9201 enter power save mode
	_powerSwitchDisable(); //!!
}

static void _powerSwitchEnable(void)
{
	digitalWrite(VDDN_EN_PIN, HIGH);
	_DelayMs(20); // Wait for VDDN to be ready.
	digitalWrite(VDDP_EN_PIN, HIGH);
	_DelayMs(20); // Wait for VDDP to be ready.
	digitalWrite(VNCP_EN_PIN, HIGH);
	_DelayMs(20);
	// Serial.printf("Power Switch Enable \r\n");
}

static void _powerSwitchDisable(void)
{
	Serial.println("Power Switch Disable Start.");
	//========================================================
	digitalWrite(VNCP_EN_PIN, LOW);
	//====== Please use _DelayMs() to wait for VNCP to be 0V ======
	// Please confirm the discharge time of VNCP power supply.
	_DelayMs(300);
	//========================================================
	digitalWrite(VDDP_EN_PIN, LOW);
	if (hw_version < HW_VER_3_3)
		_DelayMs(2000); //OK-DEV03B V3.0~V3.2
	else
		_DelayMs(300); //OK-DEV03B V3.3
	digitalWrite(VDDN_EN_PIN, LOW);

	Serial.println("Power Switch Disable Done.");
}

static unsigned int _voltageToRegisterData(unsigned int voltageData,
					   unsigned char setSelect)
{
	unsigned int registerBuf;
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
		return registerBuf = 0;
		break;
	}

	// Serial.printf(" Register Data = %3d \r\n", registerBuf);

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

	//.pmic_en_pin = _pmic_en,
	//.ist_en_pin = _ist_en,
	//.vddp_en_pin = _vddp_en,
	//.vddn_en_pin = _vddn_en,
	//.vncp_en_pin = _vncp_en,
	//.sda_pin = _sda_pin,
	//.scl_pin = _scl_pin,
	//.pg_pin = _pg_pin,
	//.ps_pin = _ps_pin,
};
