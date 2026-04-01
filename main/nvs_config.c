#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"

esp_err_t nvs_config_flash_init(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
	    ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		esp_err_t er = nvs_flash_erase();
		if (er != ESP_OK) {
			return er;
		}
		ret = nvs_flash_init();
	}
	return ret;
}

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *out)
{
	return nvs_open(NVS_CFG_NAMESPACE, mode, out);
}

esp_err_t nvs_config_get_blob(const char *key, void *out_value, size_t *length)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READONLY, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_get_blob(h, key, out_value, length);
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_set_blob(const char *key, const void *data, size_t len)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_set_blob(h, key, data, len);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_get_str(const char *key, char *out, size_t *length)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READONLY, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_get_str(h, key, out, length);
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_set_str(const char *key, const char *value)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_set_str(h, key, value);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_get_u8(const char *key, uint8_t *out)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READONLY, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_get_u8(h, key, out);
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_set_u8(const char *key, uint8_t value)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_set_u8(h, key, value);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_get_u8_or_set_default(const char *key, uint8_t default_val,
					   uint8_t *out)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_get_u8(h, key, out);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		*out = default_val;
		err = nvs_set_u8(h, key, default_val);
		if (err == ESP_OK) {
			err = nvs_commit(h);
		}
	}
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_set_wifi_sta(const char *ssid, const char *password)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	err = nvs_set_str(h, NVS_CFG_KEY_WIFI_SSID, ssid);
	if (err != ESP_OK) {
		nvs_close(h);
		return err;
	}
	err = nvs_set_str(h, NVS_CFG_KEY_WIFI_PASSWORD, password);
	if (err != ESP_OK) {
		nvs_close(h);
		return err;
	}
	err = nvs_commit(h);
	nvs_close(h);
	return err;
}

esp_err_t nvs_config_get_wifi_sta(char *ssid, size_t ssid_sz, char *pwd,
				  size_t pwd_sz)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READONLY, &h);
	if (err != ESP_OK) {
		return err;
	}
	size_t ss = ssid_sz;
	size_t ps = pwd_sz;
	(void)nvs_get_str(h, NVS_CFG_KEY_WIFI_SSID, ssid, &ss);
	(void)nvs_get_str(h, NVS_CFG_KEY_WIFI_PASSWORD, pwd, &ps);
	nvs_close(h);
	return ESP_OK;
}

esp_err_t nvs_config_set_custom_name(const char *name)
{
	nvs_handle_t h;
	esp_err_t err = open_ns(NVS_READWRITE, &h);
	if (err != ESP_OK) {
		return err;
	}
	(void)nvs_erase_key(h, NVS_CFG_KEY_CUSTOM_NAME);
	err = nvs_set_str(h, NVS_CFG_KEY_CUSTOM_NAME, name);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return err;
}
