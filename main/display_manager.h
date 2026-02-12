#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "yepd.h"
#include "esp_err.h"

// 把原本在 gatts_table.c 里的变量搬到这里
extern uint8_t *g_final_buffer; // 统一的数据缓冲区 (PSRAM)
extern uint32_t g_buffer_size; // 当前缓冲区大小


extern YEPD *gyepd;
// --- 核心接口 ---

/**
 * @brief 安全地申请 PSRAM 内存用于存放图像数据
 * @param size 需要申请的大小
 * @return uint8_t* 成功返回指针，失败返回 NULL
 */
uint8_t *display_mgr_prepare_buffer(uint32_t size);

/**
 * @brief 释放当前缓冲区并复位计数器
 */
void display_mgr_release_buffer(void);

/**
 * @brief 触发刷屏任务
 */
void display_manager_trigger_refresh(void);

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