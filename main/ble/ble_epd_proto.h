/**
 * @file ble_epd_proto.h
 * @brief Web/BLE 应用层命令协议：GATT 特征值读写载荷的编码与语义（与 gatts_table 中的特征 D/E 配合使用）。
 */
#ifndef BLE_EPD_PROTO_H
#define BLE_EPD_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include "esp_gatts_api.h"
#include "yepd.h"

/** 与前端约定的分片大小，须与网页 CHUNK_SIZE 一致。 */
#define BLE_EPD_PROTO_CHUNK_SIZE 490

typedef enum {
	BLE_EPD_CMD_RESET = 0x00,
	BLE_EPD_CMD_SET_EPD_NAME = 0x01,
	BLE_EPD_CMD_REPORT_EPD_INFO = 0x02 /**< 写入后下一次读特征 D 返回 JSON */,
	BLE_EPD_CMD_START_WRITE_DATA = 0x03,
	BLE_EPD_CMD_CURRENT_PACKET = 0x04,
	BLE_EPD_CMD_END_WRITE_DATA = 0x05,
	BLE_EPD_CMD_EPD_CLEAR = 0x06,
	BLE_EPD_CMD_BATTERY_LEVEL = 0x07,
	BLE_EPD_CMD_SET_WIFI = 0x08 /**< 写入后读特征 D 返回 STA IP */,
	BLE_EPD_CMD_SET_WORKING_MODE = 0x09,
	BLE_EPD_CMD_SET_CUSTOM_NAME = 0x0A,
	BLE_EPD_CMD_QUERY_PROGRESS = 0x0B,
} ble_epd_cmd_t;

typedef enum {
	BLE_EPD_RSP_SET_NAME_OK = 0x81,
	BLE_EPD_RSP_SET_CUSTOM_NAME_OK = 0x82,
	BLE_EPD_RSP_SET_NAME_ERR = 0x8F,
} ble_epd_rsp_t;

/** BLE 图像传输进度（HTTP 与 GATT 共用符号） */
extern uint32_t expected_total_size;
extern uint32_t received_bytes;

extern YEPD *epd;

/**
 * 特征 D：APP READ，按最近一次命令特征 E 写入的首字节（命令码）返回数据。
 */
void ble_epd_proto_on_char_read(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t trans_id,
				uint16_t attr_handle, uint16_t read_offset);

/**
 * 特征 E：APP WRITE，首字节为 ble_epd_cmd_t，后续为各命令载荷。
 * @param notify_char_handle 用于 CMD 应答 indicate 的特征 A 句柄。
 */
void ble_epd_proto_on_char_write(esp_gatt_if_t gatts_if, uint16_t conn_id,
				 uint16_t notify_char_handle, const uint8_t *data,
				 uint16_t len);

#endif
