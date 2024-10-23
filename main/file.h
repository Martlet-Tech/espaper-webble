#ifndef __FILE_H__
#define __FILE_H__

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#define SDCARD_MOUNT_POINT "/sdcard"
#define SPIFFS_MOUNT_POINT "/spiffs"
#define EXAMPLE_MAX_CHAR_SIZE 64

void init_spiffs(void);

esp_err_t sdcard_mount(void);
esp_err_t sdcard_unmount(void);
esp_err_t sdcard_test();

#endif
