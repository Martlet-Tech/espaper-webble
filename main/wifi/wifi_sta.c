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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		ESP_LOGI(TAG, "Disconnected. Retrying to connect...");
		esp_wifi_connect();
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "Successfully got IP: " IPSTR, IP2STR(&event->ip_info.ip));
	}
}

void wifi_init_sta(const char *ssid, const char *pass)
{
	static bool is_initialized = false;

	if (!is_initialized) {
		// 如果你的 app_main 里已经跑过这些，这里会自动跳过
		esp_netif_init();
		// 如果 app_main 已经创建了默认 loop，这里会返回 ESP_ERR_INVALID_STATE，可以忽略
		esp_event_loop_create_default();
		esp_netif_create_default_wifi_sta();

		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));

		ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
								    NULL, NULL));
		ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler,
								    NULL, NULL));
		is_initialized = true;
	}

	wifi_config_t wifi_config = { 0 };
	strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

	// 设置为最高安全等级扫描
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "Connecting to SSID:%s...", ssid);
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