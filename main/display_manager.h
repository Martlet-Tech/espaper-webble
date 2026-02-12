#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "yepd.h"
#include "esp_err.h"

extern YEPD *gyepd;

void display_mgr_init(void);

/**
 * @brief 安全地申请 PSRAM 内存用于存放图像数据
 * @param size 需要申请的大小
 * @return uint8_t* 成功返回指针，失败返回 NULL
 */
//uint8_t *display_mgr_prepare_buffer(uint32_t size);
uint8_t *display_mgr_prepare_user_buffer(uint32_t size);

/**
 * @brief 触发刷屏任务
 */
//void display_manager_trigger_refresh(void);
void display_mgr_trigger_user_refresh(void);

/**
 * @brief 保存当前缓冲区内容到 SPIFFS 文件
 * @param filename 文件名（不包含路径，会自动添加 /storage/ 前缀）
 * @return esp_err_t ESP_OK 成功，其他失败
 */
esp_err_t display_mgr_save_current_to_flash(const char *filename);

/**
 * @brief 清空 SPIFFS 目录下所有图片数据文件
 * @return esp_err_t ESP_OK 成功，其他失败
 */
esp_err_t display_mgr_clear_flash_images(void);

void album_mode_task(void *pvParameters);

#endif