#pragma once

#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_TAG "EPD-APP"
#define APP_INFO(fmt, ...) ESP_LOGI(APP_TAG, fmt, ##__VA_ARGS__)
#define APP_ERROR(fmt, ...) ESP_LOGE(APP_TAG, fmt, ##__VA_ARGS__)

void app_gpio_initial(void);
void app_start(void);
