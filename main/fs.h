/**
 * @file fs.h
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

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
uint8_t *SD_MMC_ReadFileToPsram(const char *path, uint32_t *file_size);
esp_err_t write_to_sdcard(const char *filepath, const char *content);

#endif
