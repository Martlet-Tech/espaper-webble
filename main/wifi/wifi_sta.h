#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

extern bool g_wifi_needs_init;

/**
 * @brief 初始化并连接 WiFi
 * * @param ssid WiFi名称
 * @param pass WiFi密码
 */
void wifi_init_sta(const char *ssid, const char *pass);

/**
 * @brief 从 NVS 读取保存的配置并自动连接
 */
void wifi_auto_reconnect(void);

/**
 * @brief 确保 WiFi 底层已初始化（netif + event loop + wifi），可安全重入
 * @return true 初始化成功
 */
bool wifi_ensure_init(void);

/**
 * @brief 执行一次完整的 WiFi 扫描，返回紧凑 JSON 数组（caller free）
 * @return JSON 字符串如 [["SSID",-65,3],...]，失败返回 "[]"
 */
char* wifi_scan_ap(void);

httpd_handle_t start_web_server(void);

#endif