#include <string.h>
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_config.h"
#include "ble_epd_proto.h"
#include "wifi_sta.h"

#include <esp_http_server.h>
#include "esp_heap_caps.h"
#include "display_manager.h"
#include "yepd.h"
#include "esp_gap_ble_api.h"
#include "cJSON.h"

bool g_wifi_needs_init = false;

static const char *TAG = "WIFI_STA";
char wifi_ip_address[16] = "0.0.0.0";

#define MAX_IMAGE_SIZE (800 * 1024)
uint8_t *img_buffer = NULL;
static httpd_handle_t server_handle = NULL;

static bool s_wifi_inited = false;
static bool s_scan_mode = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		if (!s_scan_mode) esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (!s_scan_mode) {
			ESP_LOGI(TAG, "Disconnected. Retrying to connect...");
			strcpy(wifi_ip_address, "0.0.0.0");

			vTaskDelay(pdMS_TO_TICKS(250));
			esp_wifi_connect();
		}
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		esp_ip4addr_ntoa(&event->ip_info.ip, wifi_ip_address, sizeof(wifi_ip_address));
		ESP_LOGI(TAG, "Successfully got IP: " IPSTR, IP2STR(&event->ip_info.ip));

		if (server_handle == NULL) {
			server_handle = start_web_server();
		}
	}
}

bool wifi_ensure_init(void)
{
	if (s_wifi_inited) return true;

	esp_netif_init();
	esp_err_t err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(err));
		return false;
	}
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							    &wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
							    &wifi_event_handler, NULL, NULL));

	s_wifi_inited = true;
	return true;
}

char* wifi_scan_ap(void)
{
	if (!wifi_ensure_init()) return strdup("[]");

	s_scan_mode = true;

	wifi_ap_record_t ap_info;
	bool was_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

	esp_wifi_disconnect();
	vTaskDelay(pdMS_TO_TICKS(200));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	esp_err_t ret = esp_wifi_start();
	if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
		ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
		s_scan_mode = false;
		return strdup("[]");
	}
	vTaskDelay(pdMS_TO_TICKS(100));

	esp_err_t err = esp_wifi_scan_start(NULL, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "scan start failed: %s", esp_err_to_name(err));
		s_scan_mode = false;
		return strdup("[]");
	}

	uint16_t ap_count = 0;
	esp_wifi_scan_get_ap_num(&ap_count);
	uint16_t n = MIN(ap_count, 10);
	wifi_ap_record_t *ap = malloc(n * sizeof(wifi_ap_record_t));
	if (!ap) { s_scan_mode = false; return strdup("[]"); }

	esp_wifi_scan_get_ap_records(&n, ap);

	cJSON *root = cJSON_CreateArray();
	for (int i = 0; i < n; i++) {
		cJSON *entry = cJSON_CreateArray();
		cJSON_AddItemToArray(entry, cJSON_CreateString((char*)ap[i].ssid));
		cJSON_AddItemToArray(entry, cJSON_CreateNumber(ap[i].rssi));
		cJSON_AddItemToArray(entry, cJSON_CreateNumber(ap[i].authmode));
		cJSON_AddItemToArray(root, entry);
	}
	char *json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	free(ap);

	s_scan_mode = false;

	if (was_connected) {
		ESP_LOGI(TAG, "Scan done, reconnecting...");
		esp_wifi_connect();
	}

	return json;
}

void wifi_init_sta(const char *ssid, const char *pass)
{
	static bool is_started = false;

	if (is_started) {
		ESP_LOGI(TAG, "WiFi already started, stopping for reconfiguration...");
		esp_wifi_disconnect();
		esp_wifi_stop();
		is_started = false;
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	wifi_ensure_init();

	wifi_config_t wifi_config = { 0 };
	strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	wifi_config.sta.scan_method = WIFI_FAST_SCAN;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

	esp_ble_gap_stop_advertising();
	vTaskDelay(pdMS_TO_TICKS(50));

	ESP_LOGI(TAG, "Starting WiFi and connecting to SSID:%s...", ssid);
	esp_err_t ret = esp_wifi_start();
	if (ret == ESP_OK) {
		is_started = true;
		esp_wifi_set_max_tx_power(80);
		esp_wifi_connect();
	} else {
		ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
	}
}

void wifi_auto_reconnect(void)
{
	char ssid[33] = { 0 };
	char pwd[65] = { 0 };

	if (nvs_config_get_wifi_sta(ssid, sizeof(ssid), pwd, sizeof(pwd)) != ESP_OK) {
		return;
	}
	if (strlen(ssid) > 0) {
		ESP_LOGI(TAG, "Found saved WiFi config, connecting...");
		wifi_init_sta(ssid, pwd);
	}
}

// POST 处理函数：接收电子纸数据
esp_err_t epd_data_post_handler(httpd_req_t *req)
{
	int total_len = req->content_len;
	int cur_len = 0;
	int received = 0;
	int last_log_progress = 0;

	ESP_LOGI(TAG, "Received POST request, total_len: %d", total_len);

	/*if (total_len > MAX_IMAGE_SIZE) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
		return ESP_FAIL;
	}*/
	// TODO 应该跟剩余内存比

	img_buffer = display_mgr_prepare_user_buffer(total_len);
	if (img_buffer == NULL) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Memory allocation failed");
		return ESP_FAIL;
	}

	// 设置全局变量，供蓝牙查询进度使用
	expected_total_size = total_len;
	received_bytes = 0;

	// 2. 循环读取数据流
	while (cur_len < total_len) {
		received = httpd_req_recv(req, (char *)img_buffer + cur_len, total_len - cur_len);
		if (received <= 0) { // 检查超时或错误
			if (received == HTTPD_SOCK_ERR_TIMEOUT)
				continue;
			return ESP_FAIL;
		}
		cur_len += received;
		received_bytes = cur_len; // 更新全局变量

		if (cur_len - last_log_progress > 51200) {
			ESP_LOGI(TAG, "Progress: %d / %d bytes", cur_len, total_len);
			last_log_progress = cur_len;
		}
	}

	ESP_LOGI("HTTP", "Successfully received %d bytes in PSRAM, %d packets", cur_len, received);

	// 3. 数据接收函数里的“跨域”补充
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
	httpd_resp_sendstr(req, "Data received successfully!");

	// 3. 可以在这里通知电子纸驱动去刷新 img_buffer 里的数据
	// your_epd_flush_function(img_buffer, cur_len);
	display_mgr_trigger_user_refresh();
	return ESP_OK;
}

// 处理 OPTIONS 请求，解决跨域报错
esp_err_t http_options_handler(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
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

esp_err_t clear_flash_handler(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	display_mgr_clear_flash_images();

	// 必须确保这行代码被执行！
	httpd_resp_sendstr(req, "Clear Request Received");

	// 等待 500ms 确保硬件响应
	vTaskDelay(pdMS_TO_TICKS(500));
	return ESP_OK;
}

httpd_handle_t start_web_server(void)
{
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.lru_purge_enable = true; // 自动关闭过旧的空闲连接，适合 700KB 这种大数据
	config.stack_size = 10240; // 稍微给大一点，大数据处理更稳

	if (httpd_start(&server, &config) == ESP_OK) {
		// 2. 注册 OPTIONS 接口 (必须有，否则网页 fetch 会报错)
		httpd_uri_t options_uri = {
			.uri = "/upload_epd", .method = HTTP_OPTIONS, .handler = http_options_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &options_uri);

		// 1. 注册数据接收接口
		httpd_uri_t epd_uri = {
			.uri = "/upload_epd", .method = HTTP_POST, .handler = epd_data_post_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &epd_uri);

		httpd_uri_t save_image_uri = {
			.uri = "/save_image", .method = HTTP_POST, .handler = save_image_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &save_image_uri);

		httpd_uri_t clear_flash_uri = {
			.uri = "/clear_flash", .method = HTTP_POST, .handler = clear_flash_handler, .user_ctx = NULL
		};
		httpd_register_uri_handler(server, &clear_flash_uri);

		ESP_LOGI("HTTP", "Webserver started!");
		return server;
	}
	return NULL;
}