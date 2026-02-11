#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_err.h"
#include "esp_http_server.h"
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

httpd_handle_t start_web_server(void);

#endif