#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "yepd.h"

// 把原本在 gatts_table.c 里的变量搬到这里
extern uint8_t *g_final_buffer; // 统一的数据缓冲区 (PSRAM)
extern uint32_t g_received_count; // 当前已接收到的字节数
extern uint32_t g_expected_total; // 预期接收的总大小


extern YEPD *gyepd;
// --- 核心接口 ---

/**
 * @brief 安全地申请 PSRAM 内存用于存放图像数据
 * @param size 需要申请的大小
 * @return uint8_t* 成功返回指针，失败返回 NULL
 */
uint8_t* display_mgr_prepare_buffer(uint32_t size);

/**
 * @brief 释放当前缓冲区并复位计数器
 */
void display_mgr_release_buffer(void);

/**
 * @brief 触发刷屏任务
 */
void display_manager_trigger_refresh(void);

#endif