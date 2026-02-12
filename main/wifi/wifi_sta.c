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

#include <esp_http_server.h>
#include "esp_heap_caps.h"
#include "display_manager.h"
#include "yepd.h"

bool g_wifi_needs_init = false;

static const char *TAG = "WIFI_STA";
char wifi_ip_address[16] = "0.0.0.0"; // 用于存储 IP 字符串

#define MAX_IMAGE_SIZE (800 * 1024)
uint8_t *img_buffer = NULL; // 指向 PSRAM 的指针
static httpd_handle_t server_handle = NULL; // 全局或静态变量，用于管理服务器

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		ESP_LOGI(TAG, "Disconnected. Retrying to connect...");
		strcpy(wifi_ip_address, "0.0.0.0"); // 断开连接时清空 IP

		// 如果断开了，可以选择停止服务器释放资源，也可以不刷，看你需求
		// if (server_handle) { stop_web_server(server_handle); server_handle = NULL; }

		esp_wifi_connect();
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		esp_ip4addr_ntoa(&event->ip_info.ip, wifi_ip_address, sizeof(wifi_ip_address));
		ESP_LOGI(TAG, "Successfully got IP: " IPSTR, IP2STR(&event->ip_info.ip));

		if (server_handle == NULL) {
			server_handle = start_web_server();
		}
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

// POST 处理函数：接收电子纸数据
esp_err_t epd_data_post_handler(httpd_req_t *req)
{
	int total_len = req->content_len;
	int cur_len = 0;
	int received = 0;

	/*if (total_len > MAX_IMAGE_SIZE) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
		return ESP_FAIL;
	}*/
	// TODO 应该跟剩余内存比

	img_buffer = display_mgr_prepare_buffer(total_len);
	if (img_buffer == NULL) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Memory allocation failed");
		return ESP_FAIL;
	}

	// 2. 循环读取数据流
	while (cur_len < total_len) {
		received = httpd_req_recv(req, (char *)img_buffer + cur_len, total_len - cur_len);
		if (received <= 0) { // 检查超时或错误
			if (received == HTTPD_SOCK_ERR_TIMEOUT)
				continue;
			return ESP_FAIL;
		}
		cur_len += received;
	}

	ESP_LOGI("HTTP", "Successfully received %d bytes in PSRAM", cur_len);

	// 3. 可以在这里通知电子纸驱动去刷新 img_buffer 里的数据
	// your_epd_flush_function(img_buffer, cur_len);
	display_manager_trigger_refresh();

	// 3. 数据接收函数里的“跨域”补充
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_sendstr(req, "Data received successfully!");
	return ESP_OK;
}

// 处理 OPTIONS 请求，解决跨域报错
esp_err_t http_options_handler(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}

// 处理 /save_image?time=XXXXXXXXXXXXXX
esp_err_t save_image_handler(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	char query[64];
	char timestamp[32] = "unknown";

	// 获取 URL 参数
	if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
		httpd_query_key_value(query, "time", timestamp, sizeof(timestamp));
	}

	// 调用 manager 保存
	esp_err_t res = display_mgr_save_current_to_flash(timestamp);

	if (res == ESP_OK) {
		httpd_resp_sendstr(req, "Save OK");
		return ESP_OK;
	} else {
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}
}

httpd_handle_t start_web_server(void)
{
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.lru_purge_enable = true; // 自动关闭过旧的空闲连接，适合 700KB 这种大数据
	config.stack_size = 10240; // 稍微给大一点，大数据处理更稳

	if (httpd_start(&server, &config) == ESP_OK) {
		// 1. 注册数据接收接口
		httpd_uri_t epd_uri = { .uri = "/upload_epd",
					.method = HTTP_POST,
					.handler = epd_data_post_handler, // 之前定义的处理 700KB 数据的函数
					.user_ctx = NULL };
		httpd_register_uri_handler(server, &epd_uri);

		// 2. 注册 OPTIONS 接口 (必须有，否则网页 fetch 会报错)
		httpd_uri_t options_uri = {
			.uri = "/upload_epd", .method = HTTP_OPTIONS, .handler = http_options_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &options_uri);

		httpd_uri_t save_image_uri = {
			.uri = "/save_image", .method = HTTP_POST, .handler = save_image_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &save_image_uri);

		ESP_LOGI("HTTP", "Webserver started!");
		return server;
	}
	return NULL;
}