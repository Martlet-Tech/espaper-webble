#ifndef __ARDUINO_WRAPPER
#define __ARDUINO_WRAPPER

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"

#if 1 // Timer and Delay
static inline void delay(unsigned long ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline unsigned long millis()
{
	return (unsigned long)(esp_timer_get_time() / 1000);
}
#endif // Timer and Delay

#if 1 //Serial

struct AW_Serial {
	void (*begin)(int);
	int (*printf)(const char *fmt, ...);
	void (*println)(const char *fmt);
};

extern struct AW_Serial Serial;
#endif //Serial

#if 1 // GPIO
typedef enum {
	INPUT = 0,
	OUTPUT,
	INPUT_PULLUP,
	INPUT_PULLDOWN,
	OUTPUT_OPEN_DRAIN // 新增开漏输出模式
} AW_PinMode;
typedef enum { LOW = 0, HIGH = 1 } AW_GPIO_Level;

typedef struct {
	void (*pinMode)(int pin, AW_PinMode mode);
	void (*digitalWrite)(int pin, AW_GPIO_Level level);
	int (*digitalRead)(int pin);
} AW_GPIO;

extern void pinMode(int pin, AW_PinMode mode);
extern void digitalWrite(int pin, AW_GPIO_Level level);
extern int digitalRead(int pin);

extern AW_GPIO Digital;
#endif // GPIO

#if 1 // I2C
typedef struct {
	void (*begin)(int sda_pin, int scl_pin, uint32_t freq);
	void (*beginTransmission)(uint8_t addr);
	size_t (*write)(const uint8_t *data, size_t length);
	int (*endTransmission)(void);
	void (*end)(void);
} AW_I2C;

extern AW_I2C Wire;
void check_i2c_devices(void);

#endif // I2C

#if 1 //SPI
// 定义常用常量
#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0x00 // CPOL=0, CPHA=0
#define SPI_MODE1 0x01 // CPOL=0, CPHA=1
#define SPI_MODE2 0x02 // CPOL=1, CPHA=0
#define SPI_MODE3 0x03 // CPOL=1, CPHA=1

typedef struct {
	void (*begin)(int sclk_pin, int miso_pin, int mosi_pin, int ss);
	void (*setHwCs)(bool enable); // 新增硬件CS控制
	// 时钟频率 (Hz)// 数据传输顺序 MSBFIRST/LSBFIRST// SPI模式 0-3
	void (*beginTransaction)(uint32_t clockSpeed, uint8_t bitOrder,
				 uint8_t dataMode);
	uint8_t (*transfer)(uint8_t data);
	void (*transferBytes)(const uint8_t *tx_data, uint8_t *rx_data,
			      size_t len);
	void (*endTransaction)(void);
	void (*end)(void);
} AW_SPI;

extern AW_SPI epd_spi;
#endif // SPI

#ifdef __cplusplus
}
#endif

#endif // __ARDUINO_WRAPPER
