/**
 * @file ap.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_vfs_fat.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "wifi_ap";

// 自定义函数将 MAC 地址转换为字符串
static void mac_to_str(const uint8_t *mac, char *mac_str)
{
	snprintf(mac_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
		 mac[2], mac[3], mac[4], mac[5]);
}

// 生成随机密码的函数
static void generate_random_password(char *password, size_t length)
{
	char charset[] =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	for (size_t i = 0; i < length; i++) {
		int key = esp_random() % (sizeof(charset) - 1);
		key = key;
		//password[i] = charset[key];
		password[i] = '0';
	}
	password[length] = '\0'; // 以空字符结尾
}

// Wi-Fi 事件处理回调
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
			       int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT) {
		switch (event_id) {
		case WIFI_EVENT_AP_START:
			ESP_LOGI(TAG, "Wi-Fi AP started successfully.");
			break;
		case WIFI_EVENT_AP_STACONNECTED: {
			wifi_event_ap_staconnected_t *event =
				(wifi_event_ap_staconnected_t *)event_data;
			char mac_str[18];
			mac_to_str(event->mac,
				   mac_str); // 将 MAC 地址转换为字符串
			ESP_LOGI(TAG, "Device connected: STA %s, AID=%d",
				 mac_str, event->aid);
			break;
		}
		case WIFI_EVENT_AP_STADISCONNECTED: {
			wifi_event_ap_stadisconnected_t *event =
				(wifi_event_ap_stadisconnected_t *)event_data;
			char mac_str[18];
			mac_to_str(event->mac,
				   mac_str); // 将 MAC 地址转换为字符串
			ESP_LOGI(TAG, "Device disconnected: STA %s, AID=%d",
				 mac_str, event->aid);
			break;
		}
		default:
			break;
		}
	}
}

// 启动 Wi-Fi AP 模式的函数
void wifi_init_softap()
{
	esp_netif_init();
	esp_event_loop_create_default();

	esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

	// 注册 Wi-Fi 事件处理器
	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
				   &wifi_event_handler, NULL);

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	// 获取设备的 MAC 地址
	uint8_t mac[6];
	esp_wifi_get_mac(WIFI_IF_AP, mac);

	// 将 MAC 地址转换为字符串，作为 SSID 的一部分
	char ssid[32];
	snprintf(ssid, sizeof(ssid), "EPD_%02X%02X%02X_%02X%02X%02X", mac[0],
		 mac[1], mac[2], mac[3], mac[4], mac[5]);

	// 生成随机密码
	char password[9];
	// 8位随机密码 + '\0'
	//generate_random_password(password, 8);
	memset(password, '0', 8);
	password[8] = 0;

	// 配置 Wi-Fi AP 参数
	wifi_config_t wifi_config = { .ap = {
					      .ssid = "",
					      .ssid_len = strlen(ssid),
					      .password = "",
					      .max_connection = 4,
					      .authmode =
						      WIFI_AUTH_WPA_WPA2_PSK,
				      } };

	strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
	strncpy((char *)wifi_config.ap.password, password,
		sizeof(wifi_config.ap.password));

	// 如果密码为空，将安全模式设置为开放
	if (strlen(password) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "Wi-Fi AP started with SSID: %s and Password: %s", ssid,
		 password);

	// 停止 DHCP 服务以手动配置 IP
	ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));

	// 配置静态 IP 地址
	esp_netif_ip_info_t ip_info;
	IP4_ADDR(&ip_info.ip, 192, 168, 23, 1); // 设置 AP 的 IP 地址
	IP4_ADDR(&ip_info.gw, 192, 168, 23, 1); // 设置网关
	IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // 设置子网掩码

	// 为接口分配 IP 地址
	ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));

	// 重新启动 DHCP 服务
	ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

	ESP_LOGI(TAG, "AP IP address set to: " IPSTR ".", IP2STR(&ip_info.ip));
}