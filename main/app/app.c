#include "app.h"

#include "EL133UF1.h"
#include "comm.h"
#include "pindefine.h"
#include "status.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#define UDP_BROADCAST_PORT 12345
#define TCP_PORT 8080
static int udp_broadcasting = 1;

spi_device_handle_t spi;

uint8_t *dst_image_buffer_m = NULL;
uint8_t *dst_image_buffer_s = NULL;
#define DST_FRAME_SIZE EPD_FRAME_SIZE

void app_gpio_initial(void);
void app_error(void);
void app_test_spi(void);
void get_ip_address();
void get_mac_address();
void udp_broadcast_task(void *pvParameters);
void tcp_server_task(void *pvParameters);

void app_start(void)
{
	APP_INFO("%s\n", __func__);
	dst_image_buffer_m = (uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	dst_image_buffer_s = (uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);

	app_gpio_initial();

	gpio_set_level(PIN_SW3, 1);
	delayms(20);

	gpio_set_level(PIN_SW46, 1);
	delayms(200);

	//=============   EPD Initial ========================
	initEPD();
	APP_INFO("after initepd\n");
	//====================================================

	// esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

	ESP_LOGI("WiFi", "Waiting for IP...");
	while (1) {
		char buff[32];

		esp_netif_ip_info_t ip_info;
		esp_netif_get_ip_info(netif, &ip_info);
		if (ip_info.ip.addr != 0) {
			ESP_LOGI("WiFi", "Connected with IP: %s", esp_ip4addr_ntoa(&ip_info.ip, buff, 32));
			break; // Wi-Fi 已连接，退出等待循环
		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	// 启动 UDP 广播任务
	xTaskCreate(udp_broadcast_task, "udp_broadcast", 4096, NULL, 5, NULL);

	// 启动 TCP 服务器任务
	xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);

	while (1) {
		get_ip_address();
		get_mac_address();
		vTaskDelay(pdMS_TO_TICKS(2000));
	}

	//=============   Update EPD  ========================
	while (1) {
		EL133UF1_DisplayColor(BLACK, dst_image_buffer_m, dst_image_buffer_s);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		EL133UF1_DisplayColor(WHITE, dst_image_buffer_m, dst_image_buffer_s);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		EL133UF1_DisplayColor(RED, dst_image_buffer_m, dst_image_buffer_s);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		EL133UF1_DisplayColor(GREEN, dst_image_buffer_m, dst_image_buffer_s);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		EL133UF1_DisplayColor(BLUE, dst_image_buffer_m, dst_image_buffer_s);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		/* code */
	}

	APP_INFO("after color bar\n");

	// epdDisplay();
	// APP_INFO("after edpdisplay\n");
	//====================================================
}

void app_gpio_initial(void)
{
	esp_err_t ret;

	spi_bus_config_t bus_config = {
	    .mosi_io_num = SPI_Data0_MOSI,
	    .miso_io_num = SPI_Data1_MISO,
	    .sclk_io_num = SPI_CLK,
	    .quadwp_io_num = -1,
	    .quadhd_io_num = -1,
	    .max_transfer_sz = CHUNK_SIZE,
	};
	ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	spi_device_interface_config_t dev_config_0 = {
	    .clock_speed_hz = 12000000,
	    .mode = 0,
	    .spics_io_num = -1,
	    .queue_size = 7,
	    .command_bits = 0,
	    .address_bits = 0,
	    .dummy_bits = 0,
	    //.duty_cycle_pos = 128,
	    //.flags = SPI_DEVICE_HALFDUPLEX, // 使用半双工模式
	};

	// TEST_ESP_OK(spi_bus_initialize(TEST_SPI_HOST, &buscfg, dma ? SPI_DMA_CH_AUTO : 0));
	ret = spi_bus_add_device(SPI2_HOST, &dev_config_0, &spi);
	if (ret != ESP_OK) {
		printf("spi bus initial failed\r\n");
		while (1) {
			vTaskDelay(1000);
		}
	}

	gpio_config_t gpiocfg_out_lcd = {};
	gpiocfg_out_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_out_lcd.mode = GPIO_MODE_OUTPUT;
	gpiocfg_out_lcd.pin_bit_mask = (1ULL << EPD_RST) | (1ULL << PIN_CS_M) | (1ULL << PIN_CS_S) | (1ULL << PIN_SW3) | (1ULL << PIN_SW46);
	gpiocfg_out_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_out_lcd.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&gpiocfg_out_lcd);

	gpio_config_t gpiocfg_in_lcd = {};
	gpiocfg_in_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_in_lcd.mode = GPIO_MODE_INPUT;
	gpiocfg_in_lcd.pin_bit_mask = (1ULL << EPD_BUSY);
	gpiocfg_in_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_in_lcd.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&gpiocfg_in_lcd);
}

void app_error(void)
{
	APP_INFO("APP error\n");

	while (1) {
		vTaskDelay(1000);
	}
}
//===========================

void get_ip_address()
{
	// 获取默认的网络接口 (STA)
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

	if (netif) {
		// 创建一个 IP 信息结构体
		esp_netif_ip_info_t ip_info;

		// 获取 IP 信息
		if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {

			char buff[32];

			// 打印 IP 地址

			ESP_LOGI("WiFi", "IP Address: %s", esp_ip4addr_ntoa(&ip_info.ip, buff, 32));
			ESP_LOGI("WiFi", "Subnet Mask: %s", esp_ip4addr_ntoa(&ip_info.netmask, buff, 32));
			ESP_LOGI("WiFi", "Gateway: %s", esp_ip4addr_ntoa(&ip_info.gw, buff, 32));
		} else {
			ESP_LOGI("WiFi", "Failed to get IP address");
		}
	} else {
		ESP_LOGI("WiFi", "Network interface not found");
	}
}

void get_mac_address()
{
	uint8_t mac[6];					    // 用于存储 MAC 地址
	esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac); // 获取 STA 模式的 MAC 地址

	if (err == ESP_OK) {
		// 打印 MAC 地址
		ESP_LOGI("WiFi", "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		ESP_LOGI("WiFi", "Failed to get MAC address");
	}
}

void udp_broadcast_task(void *pvParameters)
{
	char message[64];
	char ip_str[16];
	int sock;
	struct sockaddr_in broadcast_addr;

	// 创建 UDP 广播套接字
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_port = htons(UDP_BROADCAST_PORT);
	broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

	int broadcast_enable = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

	while (1) {
		if (udp_broadcasting) {
			esp_netif_ip_info_t ip_info;
			esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
			esp_netif_get_ip_info(netif, &ip_info);
			sprintf(ip_str, "%s", ip4addr_ntoa(&ip_info.ip));

			sprintf(message, "esp-epd-%s", ip_str);
			sendto(sock, message, strlen(message), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
			ESP_LOGI("UDP Broadcast", "Broadcasting: %s", message);
		}
		vTaskDelay(5000 / portTICK_PERIOD_MS); // 每 5 秒广播一次
	}

	close(sock);
	vTaskDelete(NULL);
}

void tcp_server_task(void *pvParameters)
{
	int listen_sock, conn_sock;
	struct sockaddr_in server_addr, client_addr;
	socklen_t addr_len = sizeof(client_addr);
	char rx_buffer[1024];

	// 创建 TCP 监听套接字
	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(TCP_PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
	listen(listen_sock, 1);

	while (1) {
		// 接受 TCP 连接
		ESP_LOGI("TCP Server", "Waiting for TCP connection...");
		conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
		if (conn_sock >= 0) {
			ESP_LOGI("TCP Server", "TCP connection established, stopping UDP broadcast");
			udp_broadcasting = 0; // 停止 UDP 广播

			// 接收消息
			while (1) {
				int len = recv(conn_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
				if (len <= 0) {
					ESP_LOGI("TCP Server", "Connection closed");
					break; // 连接关闭
				}
				rx_buffer[len] = 0; // 将接收到的数据作为字符串处理
				ESP_LOGI("TCP Server", "Received: %s", rx_buffer);
			}

			close(conn_sock);
			udp_broadcasting = 1; // 恢复 UDP 广播
			ESP_LOGI("TCP Server", "TCP connection closed, restarting UDP broadcast");
		}
	}

	close(listen_sock);
	vTaskDelete(NULL);
}
