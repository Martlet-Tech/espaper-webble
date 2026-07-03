/**
 * @file ble_epd_proto.c
 * @brief 网页通过 Web BLE 与固件之间的命令/应答实现。
 */

#include "ble_epd_proto.h"

#include <stdlib.h>
#include <string.h>
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_pm.h"

#include "cJSON.h"
#include "nvs_config.h"

#include "bsp.h"
#include "display_manager.h"
#include "wifi_sta.h"

static const char TAG[] = "ble_epd_proto";

extern char wifi_ip_address[16];

YEPD *epd = NULL;
uint32_t expected_total_size = 0;
uint32_t received_bytes = 0;

static uint8_t *s_ble_rx_buffer;
static ble_epd_cmd_t s_pending_read_cmd;
static uint32_t s_packet_log_counter;

/* WiFi 扫描结果（互斥保护） */
static SemaphoreHandle_t s_scan_mutex = NULL;
static char *s_wifi_scan_result = NULL;
static bool s_wifi_scan_done = false;

static void wifi_scan_task(void *pv)
{
	char *json = wifi_scan_ap();
	if (xSemaphoreTake(s_scan_mutex, portMAX_DELAY) == pdTRUE) {
		if (s_wifi_scan_result) free(s_wifi_scan_result);
		s_wifi_scan_result = json;
		s_wifi_scan_done = true;
		xSemaphoreGive(s_scan_mutex);
	} else {
		free(json);
	}
	vTaskDelete(NULL);
}

static void display_clear_task(void *pvParameter)
{
	uint8_t color_index = (uint8_t)(uintptr_t)pvParameter;
	ESP_LOGI(TAG, "clear with color index %02x", color_index);

	if (epd && epd->clear) {
		epd->clear(color_index);
	} else {
		ESP_LOGE(TAG, "epd->clear is NULL");
	}

	vTaskDelete(NULL);
}

static void proto_read_report_epd_info(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t trans_id,
				       uint16_t attr_handle, uint16_t read_offset)
{
	cJSON *root = NULL;
	esp_gatt_rsp_t rsp;
	memset(&rsp, 0, sizeof(rsp));

	size_t required_len = 0;
	(void)nvs_config_get_blob(NVS_CFG_KEY_CONFIG_DATA, NULL, &required_len);
	if (required_len == 0) {
		ESP_LOGE(TAG, "config_data not found");
	}

	uint8_t *buffer = malloc(required_len + 1);
	if (buffer) {
		buffer[required_len] = '\0';
		(void)nvs_config_get_blob(NVS_CFG_KEY_CONFIG_DATA, buffer, &required_len);
		buffer[required_len] = '\0';
	}

	epd = buffer ? yepd_find_by_name((const char *)buffer) : NULL;

	if (epd != NULL) {
		ESP_LOGI(TAG, "read epd name %s", epd->name);
		root = cJSON_CreateObject();
		if (root) {
			cJSON_AddStringToObject(root, "name", epd->name);
			cJSON_AddNumberToObject(root, "width", epd->width);
			cJSON_AddNumberToObject(root, "height", epd->height);
			cJSON_AddStringToObject(root, "palette", epd->palette);
			cJSON_AddNumberToObject(root, "bpp", epd->bpp);
			char *json_str = cJSON_PrintUnformatted(root);
			if (json_str) {
				rsp.attr_value.len = strlen(json_str);
				rsp.attr_value.handle = attr_handle;
				rsp.attr_value.offset = read_offset;
				rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
				memcpy(rsp.attr_value.value, json_str, rsp.attr_value.len);
				free(json_str);
			}
		}
	} else {
		ESP_LOGE(TAG, "invalid or missing EPD name in NVS");
	}

	esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);

	vTaskDelay(pdMS_TO_TICKS(1000));
	if (root) {
		cJSON_Delete(root);
	}
	free(buffer);
}

static void proto_read_wifi_ip(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t trans_id, uint16_t attr_handle,
			       uint16_t read_offset)
{
	esp_gatt_rsp_t rsp;
	memset(&rsp, 0, sizeof(rsp));
	rsp.attr_value.handle = attr_handle;
	rsp.attr_value.offset = read_offset;
	rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
	uint16_t ip_len = (uint16_t)strlen(wifi_ip_address);
	rsp.attr_value.len = ip_len;
	memcpy(rsp.attr_value.value, wifi_ip_address, ip_len);

	ESP_LOGI(TAG, "Responding WiFi IP to web: %s", wifi_ip_address);
	esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);
}

static void proto_read_progress(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t trans_id, uint16_t attr_handle,
				uint16_t read_offset)
{
	esp_gatt_rsp_t rsp;
	memset(&rsp, 0, sizeof(rsp));
	rsp.attr_value.handle = attr_handle;
	rsp.attr_value.offset = read_offset;
	rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;

	uint16_t progress = 0;
	if (expected_total_size > 0) {
		progress = (uint16_t)((received_bytes * 100) / expected_total_size);
	}
	rsp.attr_value.len = 2;
	rsp.attr_value.value[0] = (uint8_t)((progress >> 8) & 0xFF);
	rsp.attr_value.value[1] = (uint8_t)(progress & 0xFF);

	ESP_LOGI(TAG, "Query progress: %u%% (received: %lu / total: %lu)", progress, (unsigned long)received_bytes,
		 (unsigned long)expected_total_size);
	esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);
}

void ble_epd_proto_on_char_read(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t trans_id, uint16_t attr_handle,
				uint16_t read_offset)
{
	ESP_LOGI(TAG, "READ pending cmd = %d", (int)s_pending_read_cmd);

	switch (s_pending_read_cmd) {
	case BLE_EPD_CMD_REPORT_EPD_INFO:
		proto_read_report_epd_info(gatts_if, conn_id, trans_id, attr_handle, read_offset);
		break;
	case BLE_EPD_CMD_SET_WIFI:
		proto_read_wifi_ip(gatts_if, conn_id, trans_id, attr_handle, read_offset);
		break;
	case BLE_EPD_CMD_QUERY_PROGRESS:
		proto_read_progress(gatts_if, conn_id, trans_id, attr_handle, read_offset);
		break;
	case BLE_EPD_CMD_WIFI_SCAN: {
		esp_gatt_rsp_t rsp;
		memset(&rsp, 0, sizeof(rsp));
		rsp.attr_value.handle = attr_handle;
		rsp.attr_value.offset = read_offset;
		rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;

		if (s_scan_mutex && xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
			if (s_wifi_scan_done && s_wifi_scan_result) {
				uint16_t len = strlen(s_wifi_scan_result);
				rsp.attr_value.len = MIN(len, 499);
				memcpy(rsp.attr_value.value, s_wifi_scan_result, rsp.attr_value.len);
			} else {
				const char *p = "{\"pending\":1}";
				rsp.attr_value.len = strlen(p);
				memcpy(rsp.attr_value.value, p, rsp.attr_value.len);
			}
			xSemaphoreGive(s_scan_mutex);
		}
		esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);
		break;
	}
	default:
		break;
	}
}

static void proto_write_set_epd_name(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t notify_char_handle,
				     const uint8_t *payload, uint16_t payload_len)
{
	epd = yepd_find_by_name((const char *)payload);
	if (epd != NULL) {
		ESP_LOGI(TAG, "set epd name %s", epd->name);
		esp_err_t err = nvs_config_set_blob(NVS_CFG_KEY_CONFIG_DATA, payload, payload_len);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "NVS set_blob failed: %s", esp_err_to_name(err));
		} else {
			ESP_LOGI(TAG, "Data saved to NVS successfully");
		}
		uint8_t rsp[] = { BLE_EPD_RSP_SET_NAME_OK };
		esp_ble_gatts_send_indicate(gatts_if, conn_id, notify_char_handle, sizeof(rsp), rsp, false);
		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
	} else {
		ESP_LOGE(TAG, "invalid epd name %s", (const char *)payload);
		uint8_t rsp[] = { BLE_EPD_RSP_SET_NAME_ERR };
		esp_err_t err =
			esp_ble_gatts_send_indicate(gatts_if, conn_id, notify_char_handle, sizeof(rsp), rsp, false);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "send indicate failed: %s", esp_err_to_name(err));
		} else {
			ESP_LOGI(TAG, "send indicate success");
		}
	}
}

static void proto_write_start_data(const uint8_t *data, uint16_t len)
{
	ESP_LOGI(TAG, "CMD_START_WRITE_DATA");
	s_packet_log_counter = 0;

	if (len >= 5) {
		esp_pm_lock_acquire(s_pm_cpu_lock);
		vTaskDelay(pdMS_TO_TICKS(50));

		expected_total_size = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) |
				      (uint32_t)data[4];
		received_bytes = 0;
		s_ble_rx_buffer = (uint8_t *)display_mgr_prepare_user_buffer(expected_total_size);
		if (s_ble_rx_buffer == NULL) {
			ESP_LOGE(TAG, "malloc fail, size: %lu", (unsigned long)expected_total_size);
		} else {
			ESP_LOGI(TAG, "RX start, expect %lu bytes", (unsigned long)expected_total_size);
		}
	}
}

static void proto_write_current_packet(const uint8_t *data, uint16_t len)
{
	s_packet_log_counter++;
	if (s_packet_log_counter % 50 == 0) {
		ESP_LOGI(TAG, "RX chunk count: %lu", (unsigned long)s_packet_log_counter);
	}

	if (s_ble_rx_buffer && len > 5) {
		uint32_t pkt_idx = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) |
				   (uint32_t)data[4];
		uint32_t offset = pkt_idx * BLE_EPD_PROTO_CHUNK_SIZE;
		uint16_t payload_len = len - 5;

		if (offset + payload_len > expected_total_size) {
			payload_len = (uint16_t)(expected_total_size - offset);
			ESP_LOGW(TAG, "tail trim payload to %u", (unsigned)payload_len);
		}
		if (offset < expected_total_size) {
			memcpy(s_ble_rx_buffer + offset, &data[5], payload_len);
			received_bytes += payload_len;
		}
	}
}

static void proto_write_end_data(void)
{
	ESP_LOGI(TAG, "CMD_END_WRITE_DATA");
	ESP_LOGI(TAG, "RX done: %lu / expect %lu", (unsigned long)received_bytes, (unsigned long)expected_total_size);

	if (received_bytes == expected_total_size) {
		display_mgr_trigger_user_refresh();
	} else {
		ESP_LOGE(TAG, "incomplete RX: %lu / %lu", (unsigned long)received_bytes,
			 (unsigned long)expected_total_size);
	}
	esp_pm_lock_release(s_pm_cpu_lock);
}

static void proto_write_set_wifi(const uint8_t *data, uint16_t len)
{
	if (len < 3) {
		ESP_LOGV(TAG, "WiFi data too short (min 3 bytes) — query frame, ignored");
		return;
	}
	uint8_t ssid_len = data[1];
	uint8_t pwd_len = data[2];
	if (len < (3u + ssid_len + pwd_len)) {
		ESP_LOGE(TAG, "WiFi packet length mismatch: need %u, got %u", 3u + ssid_len + pwd_len, len);
		return;
	}
	char ssid[33] = { 0 };
	char pwd[65] = { 0 };
	uint8_t copy_ssid = (ssid_len > 32) ? 32 : ssid_len;
	uint8_t copy_pwd = (pwd_len > 64) ? 64 : pwd_len;
	memcpy(ssid, &data[3], copy_ssid);
	memcpy(pwd, &data[3 + ssid_len], copy_pwd);

	ESP_LOGI(TAG, "WiFi cfg SSID=[%s]", ssid);
	esp_err_t err = nvs_config_set_wifi_sta(ssid, pwd);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "WiFi NVS save failed: %s", esp_err_to_name(err));
	} else {
		ESP_LOGI(TAG, "WiFi config saved to NVS");
	}
	g_wifi_needs_init = true;
}

void ble_epd_proto_on_char_write(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t notify_char_handle,
				 const uint8_t *data, uint16_t len)
{
	if (len < 1) {
		return;
	}

	s_pending_read_cmd = (ble_epd_cmd_t)data[0];

	switch (s_pending_read_cmd) {
	case BLE_EPD_CMD_RESET:
		ESP_LOGI(TAG, "CMD_RESET_EPD");
		vTaskDelay(pdMS_TO_TICKS(2000));
		esp_restart();
		break;

	case BLE_EPD_CMD_SET_EPD_NAME:
		ESP_LOGI(TAG, "CMD_SET_EPD_NAME");
		if (len > 1) {
			proto_write_set_epd_name(gatts_if, conn_id, notify_char_handle, data + 1, len - 1);
		}
		break;

	case BLE_EPD_CMD_REPORT_EPD_INFO:
		ESP_LOGI(TAG, "CMD_REPORT_EPD_INFO");
		break;

	case BLE_EPD_CMD_START_WRITE_DATA:
		proto_write_start_data(data, len);
		break;

	case BLE_EPD_CMD_CURRENT_PACKET:
		proto_write_current_packet(data, len);
		break;

	case BLE_EPD_CMD_END_WRITE_DATA:
		proto_write_end_data();
		break;

	case BLE_EPD_CMD_EPD_CLEAR: {
		ESP_LOGI(TAG, "CMD_EPD_CLEAR");
		uint8_t color_index = (len > 1) ? data[1] : 0;
		xTaskCreate(display_clear_task, "clear_scr", 4096, (void *)(uintptr_t)color_index, 5, NULL);
		break;
	}

	case BLE_EPD_CMD_SET_WIFI:
		ESP_LOGI(TAG, "CMD_SET_WIFI");
		proto_write_set_wifi(data, len);
		break;

	case BLE_EPD_CMD_SET_WORKING_MODE:
		ESP_LOGI(TAG, "CMD_SET_WORKING_MODE");
		if (len > 1) {
			uint8_t new_mode = data[1];
			ESP_LOGI(TAG, "set working mode %u", (unsigned)new_mode);
			esp_err_t err = nvs_config_set_u8(NVS_CFG_KEY_WORKING_MODE, new_mode);
			if (err == ESP_OK) {
				ESP_LOGI(TAG, "working_mode saved, restart...");
				vTaskDelay(pdMS_TO_TICKS(500));
				esp_restart();
			} else {
				ESP_LOGE(TAG, "NVS working_mode failed: %s", esp_err_to_name(err));
			}
		} else {
			ESP_LOGW(TAG, "Invalid length for CMD_SET_WORKING_MODE");
		}
		break;

	case BLE_EPD_CMD_WIFI_SCAN:
		ESP_LOGI(TAG, "CMD_WIFI_SCAN");
		if (s_scan_mutex == NULL)
			s_scan_mutex = xSemaphoreCreateMutex();
		s_wifi_scan_done = false;
		xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 3, NULL);
		break;

	case BLE_EPD_CMD_SET_CUSTOM_NAME:
		ESP_LOGI(TAG, "CMD_SET_CUSTOM_NAME");
		if (len > 1) {
			char name[33] = { 0 };
			size_t name_len = len - 1;
			if (name_len > sizeof(name) - 1) {
				name_len = sizeof(name) - 1;
			}
			memcpy(name, &data[1], name_len);
			ESP_LOGI(TAG, "custom name: %s", name);

			esp_err_t err = nvs_config_set_custom_name(name);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "NVS custom_name failed: %s", esp_err_to_name(err));
			} else {
				ESP_LOGI(TAG, "custom name saved to NVS");
			}
			uint8_t rsp[] = { BLE_EPD_RSP_SET_CUSTOM_NAME_OK };
			esp_ble_gatts_send_indicate(gatts_if, conn_id, notify_char_handle, sizeof(rsp), rsp, false);
			vTaskDelay(pdMS_TO_TICKS(1000));
			esp_restart();
		}
		break;

	default:
		ESP_LOGW(TAG, "unknown cmd 0x%02x", data[0]);
		break;
	}
}
