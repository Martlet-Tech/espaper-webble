file(GLOB LCD_SOURCES "lcd/*.c")

idf_component_register(
	SRCS 		"main.c"
			"ugui/ugui.c"
			"QRLib/qr_encode.c"
			"utils.c"
			"bsp.c"
			"net/ap.c"
			"net/http_server.c"
			"img_proc/img_proc.c"
			${LCD_SOURCES}
			"pmic/IST9201.c"
			"arduino_wrapper.c"

        INCLUDE_DIRS 	"."
			"lcd"
			"net"
			"web"
			"img_proc"
			"ugui"
			"QRLib"
			"pmic"
	# EMBED_FILES 	"web/favicon.ico" "web/upload_script.html"
)
