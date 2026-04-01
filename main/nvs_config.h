#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/** Application NVS namespace (matches historical "storage" partition usage). */
#define NVS_CFG_NAMESPACE "storage"

#define NVS_CFG_KEY_CONFIG_DATA "config_data"
#define NVS_CFG_KEY_CUSTOM_NAME "custom_name"
#define NVS_CFG_KEY_WORKING_MODE "working_mode"
#define NVS_CFG_KEY_WIFI_SSID "wifi_ssid"
#define NVS_CFG_KEY_WIFI_PASSWORD "wifi_password"

/**
 * @brief Standard NVS flash init (handles NO_FREE_PAGES / NEW_VERSION_FOUND).
 */
esp_err_t nvs_config_flash_init(void);

/**
 * @brief Thin wrappers: open "storage", one operation, close.
 *        Semantics match nvs_get_* / nvs_set_* (see ESP-IDF docs for length rules).
 */
esp_err_t nvs_config_get_blob(const char *key, void *out_value, size_t *length);
esp_err_t nvs_config_set_blob(const char *key, const void *data, size_t len);

esp_err_t nvs_config_get_str(const char *key, char *out, size_t *length);
esp_err_t nvs_config_set_str(const char *key, const char *value);

esp_err_t nvs_config_get_u8(const char *key, uint8_t *out);
esp_err_t nvs_config_set_u8(const char *key, uint8_t value);

/**
 * @brief READWRITE: read u8 or write default and commit if key missing.
 */
esp_err_t nvs_config_get_u8_or_set_default(const char *key, uint8_t default_val,
					   uint8_t *out);

/**
 * @brief Save both WiFi strings and commit (used by BLE provisioning).
 */
esp_err_t nvs_config_set_wifi_sta(const char *ssid, const char *password);

/**
 * @brief Load WiFi strings if present (ignores per-key NOT_FOUND like legacy code).
 *        Returns error only if namespace cannot be opened.
 */
esp_err_t nvs_config_get_wifi_sta(char *ssid, size_t ssid_sz, char *pwd,
				  size_t pwd_sz);

/**
 * @brief Erase custom_name then set as string (type cleanup + commit).
 */
esp_err_t nvs_config_set_custom_name(const char *name);

#endif
