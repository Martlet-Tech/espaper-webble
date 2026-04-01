file(GLOB LCD_SOURCES "lcd/*.c")

idf_component_register(
	SRCS 		"main.c"
			"nvs_config.c"
			"ugui/ugui.c"
			"QRLib/qr_encode.c"
			"utils.c"
			"bsp.c"
			"net/ap.c"
			"net/http_server.c"
			"img_proc/img_proc.c"
			${LCD_SOURCES}
			"pmic/IST9201.c"
			"ble/gatts_table.c"
			"wifi/wifi_sta.c"
			"display_manager.c"

        INCLUDE_DIRS 	"."
			"lcd"
			"net"
			"web"
			"img_proc"
			"ugui"
			"QRLib"
			"pmic"
			"ble"
			"wifi"
	# EMBED_FILES 	"web/favicon.ico" "web/upload_script.html"
	PRIV_REQUIRES bt nvs_flash
	REQUIRES driver spi_flash fatfs sdmmc spiffs vfs esp_http_server

)
