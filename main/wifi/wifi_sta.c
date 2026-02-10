#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_sta.h"

static const char *TAG = "WIFI_STA";
char wifi_ip_address[16] = "0.0.0.0"; // 用于存储 IP 字符串

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		ESP_LOGI(TAG, "Disconnected. Retrying to connect...");
		strcpy(wifi_ip_address, "0.0.0.0"); // 断开连接时清空 IP
		esp_wifi_connect();
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		esp_ip4addr_ntoa(&event->ip_info.ip, wifi_ip_address, sizeof(wifi_ip_address));
		ESP_LOGI(TAG, "Successfully got IP: " IPSTR, IP2STR(&event->ip_info.ip));
	}
}

void wifi_init_sta(const char *ssid, const char *pass)
{
	static bool is_initialized = false;

	static bool is_started = false;

	// 1. 如果已经启动过，先停止，防止配置冲突和瞬时大电流叠加
	if (is_started) {
		ESP_LOGI(TAG, "WiFi already started, stopping for reconfiguration...");
		esp_wifi_disconnect();
		esp_wifi_stop();
		is_started = false;
		vTaskDelay(pdMS_TO_TICKS(100)); // 给硬件一点喘息时间
	}

	// 2. 基础初始化（整个生命周期只做一次）
	if (!is_initialized) {
		esp_netif_init();
		if (esp_event_loop_create_default() != ESP_OK) {
			ESP_LOGW(TAG, "Event loop already exists.");
		}
		esp_netif_create_default_wifi_sta();

		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));

		ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
								    NULL, NULL));
		ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler,
								    NULL, NULL));
		is_initialized = true;
	}

	// 3. 配置 WiFi 参数
	wifi_config_t wifi_config = { 0 };
	strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	// 💡 关键：开启快速扫描或特定信道扫描可以减少 RF 工作时间，降低功耗
	wifi_config.sta.scan_method = WIFI_FAST_SCAN;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

	// 4. 启动 WiFi
	ESP_LOGI(TAG, "Starting WiFi and connecting to SSID:%s...", ssid);
	esp_err_t ret = esp_wifi_start();
	if (ret == ESP_OK) {
		is_started = true;
		// 💡 绝招：限制发射功率。80 代表 20dBm（最大），可以试着降到 50-60 (12.5dBm - 15dBm)
		// 这能显著降低瞬间峰值电流，防止 Brownout 重启
		esp_wifi_set_max_tx_power(60);
		esp_wifi_connect();
	} else {
		ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
	}
}

void wifi_auto_reconnect(void)
{
	nvs_handle_t my_handle;
	char ssid[33] = { 0 };
	char pwd[65] = { 0 };
	size_t size;

	if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
		size = sizeof(ssid);
		nvs_get_str(my_handle, "wifi_ssid", ssid, &size);
		size = sizeof(pwd);
		nvs_get_str(my_handle, "wifi_password", pwd, &size);
		nvs_close(my_handle);

		if (strlen(ssid) > 0) {
			ESP_LOGI(TAG, "Found saved WiFi config, connecting...");
			wifi_init_sta(ssid, pwd);
		}
	}
}