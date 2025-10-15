/**
 * @file bsp.c
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-10
 * 
 * @copyright zhaitao.as@outlook.com (c) 2025
 * 
 */

#include "bsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include <esp_timer.h>

#include "qr_encode.h"
#include "cJSON.h"

// 自定义头文件，确保它们不是重复的
#include "ap.h"
#include "http_server.h"
#include "bsp.h"
#include "img_proc.h"
#include "utils.h"
#include "yepd_if.h"
#include "yepd.h"

#define MAX_FILES 100 // 假设最大图片数量为 100

extern int display_debug;
extern int display_show_last;
extern int show_qr;

extern YEPD *gyepd; // global epd pointer, defined in bsp.c

void bsp_gpio_initial(void);

void process_config(const char *file_path);

static const char *TAG = "bsp.c";

sdmmc_card_t *card;

YEPD *gyepd;

int image_numbers[MAX_FILES]; // 存储所有图片的编号
int image_count = 0; // 图片数量
int current_image_number = -1; // 当前图片编号

// 函数声明
void show_next_image(YEPD *epd);
void show_prev_image(YEPD *epd);
void delete_current_image();

/* 打印文件列表 */
void list_files(const char *base_path)
{
	DIR *dir = opendir(base_path);
	if (!dir) {
		ESP_LOGE(TAG, "无法打开目录: %s", base_path);
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		ESP_LOGI(TAG, "找到文件: %s", entry->d_name);
	}
	closedir(dir);
}

void init_spiffs(void)
{
	// 配置 SPIFFS
	esp_vfs_spiffs_conf_t conf = {
		.base_path = SPIFFS_MOUNT_POINT, // 挂载路径
		.partition_label = NULL, // 默认使用 spiffs 分区
		.max_files = 5, // 最大打开文件数量
		.format_if_mount_failed = true // 如果挂载失败，则格式化
	};

	// 挂载 SPIFFS
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",
				 esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
			 esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
	}

	// 打印文件列表
	list_files("/spiffs");
}

esp_err_t sdcard_mount()
{
	esp_err_t ret;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};

	const char mount_point[] = SDCARD_MOUNT_POINT;
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	slot_config.width = 4;
	slot_config.clk = 42;
	slot_config.cmd = 41;
	slot_config.d0 = 2;
	slot_config.d1 = 1;
	slot_config.d2 = 39;
	slot_config.d3 = 40;
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config,
				      &mount_config, &card);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(
				TAG,
				"Failed to mount filesystem. "
				"If you want the card to be formatted, set format_if_mount_failed = true.");
		} else {
			ESP_LOGE(
				TAG,
				"Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.",
				esp_err_to_name(ret));
		}
		return ESP_FAIL;
	}
	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);

	return ESP_OK;
}

esp_err_t sdcard_unmount()
{
	return esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, card);
}

esp_err_t sdcard_test()
{
	const char TAG[] = "sdmmc_card_test";
	const char file_path[] = SDCARD_MOUNT_POINT "/test.txt";

	// 创建并打开文件
	FILE *f = fopen(file_path, "w+");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 写入内容到文件
	fprintf(f, "Hello ESP32!\n");
	fclose(f);

	// 打开文件进行读取
	f = fopen(file_path, "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 读取文件内容
	char line[128];
	fgets(line, sizeof(line), f);
	printf("Read from file: %s", line);
	fclose(f);

	if (unlink(file_path) != 0) {
		ESP_LOGE(TAG, "Failed to delete file: %s", file_path);
	}

	ESP_LOGI(TAG, "OK");
	return ESP_OK;
}

uint8_t *SD_MMC_ReadFileToPsram(const char *path, uint32_t *file_size)
{
	// 打开文件
	FILE *file = fopen(path, "r");
	if (file == NULL) {
		ESP_LOGE("SD_MMC", "Failed to open file for reading");
		return NULL;
	}

	// 获取文件大小
	fseek(file, 0, SEEK_END);
	long len = ftell(file);
	fseek(file, 0, SEEK_SET);

	// 分配PSRAM内存用于存储图像
	uint8_t *image_buffer = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
	if (image_buffer == NULL) {
		ESP_LOGE("SD_MMC", "heap_caps_malloc failed");
		fclose(file);
		return NULL;
	}

	// 读取文件到PSRAM
	size_t bytesRead = fread(image_buffer, 1, len, file);
	if (bytesRead != len) {
		ESP_LOGE("SD_MMC", "File read failed");
		heap_caps_free(image_buffer);
		fclose(file);
		return NULL;
	}

	// 关闭文件
	fclose(file);

	if (file_size != NULL) {
		*file_size = len;
	}

	return image_buffer;
}

esp_err_t write_to_sdcard(const char *filepath, const char *content)
{
	// 创建并打开文件
	FILE *f = fopen(filepath, "w+");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		sdcard_unmount();
		return ESP_FAIL;
	}

	// 写入内容到文件
	int fret = fprintf(f, "%s", content);
	if (fret < 0) {
		// fprintf 失败
		ESP_LOGE(TAG, "Failed to write to file");
		fclose(f);
		sdcard_unmount();
		return ESP_FAIL;
	} else if (fret != strlen(content)) {
		// 写入的字符数与字符串长度不符，可能存在部分写入失败的情况
		ESP_LOGW(TAG, "Partial write to file: expected %zu, wrote %d",
			 strlen(content), fret);
	}

	fclose(f);
	return ESP_OK;
}

void sdcard_save_buff(uint8_t *buff, int size, const char *file)
{
	// 检查输入参数
	if (buff == NULL || file == NULL || size <= 0) {
		ESP_LOGE(TAG, "Invalid arguments: buff=%p, size=%d, file=%s",
			 buff, size, file);
		return;
	}

	// 打开文件
	FILE *f = fopen(file, "wb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file: %s", file);
		return;
	}

	// 写入数据
	size_t written = fwrite(buff, 1, size, f);
	if (written != size) {
		ESP_LOGE(TAG,
			 "Failed to write all data to file: %s. Written: %d/%d",
			 file, (int)written, size);
	} else {
		ESP_LOGI(TAG, "Successfully saved %d bytes to %s", size, file);
	}

	// 关闭文件
	fclose(f);
}

esp_err_t bsp_create_wifi_qr_str(char **str_buf)
{
	wifi_config_t wifi_config;
	esp_err_t ret = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get AP config: %s",
			 esp_err_to_name(ret));
		return ret;
	}

	/* 计算需要的内存大小 */
	int needed_size = snprintf(NULL, 0, "WIFI:T:WPA;S:%s;P:%s;;",
				   wifi_config.ap.ssid,
				   wifi_config.ap.password);
	if (needed_size < 0) {
		return ESP_FAIL;
	}

	/* 动态申请内存 */
	*str_buf = heap_caps_malloc(needed_size + 1, MALLOC_CAP_SPIRAM);
	if (*str_buf == NULL) {
		return ESP_ERR_NO_MEM;
	}

	/* 生成最终字符串 */
	sprintf(*str_buf, "WIFI:T:WPA;S:%s;P:%s;;", wifi_config.ap.ssid,
		wifi_config.ap.password);

	return ESP_OK;
}

esp_err_t bsp_create_web_qr_str(char **str_buf)
{
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

	if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get IP address");
		return ESP_FAIL;
	}

	/* 提取IP地址的四个字节 */
	uint8_t *ip_bytes = (uint8_t *)&ip_info.ip.addr;
	const uint8_t ip1 = ip_bytes[0];
	const uint8_t ip2 = ip_bytes[1];
	const uint8_t ip3 = ip_bytes[2];
	const uint8_t ip4 = ip_bytes[3];

	/* 计算需要的内存大小 */
	int needed_size =
		snprintf(NULL, 0, "http://%u.%u.%u.%u/", ip1, ip2, ip3, ip4);
	if (needed_size < 0) {
		return ESP_FAIL;
	}

	/* 动态申请内存 */
	*str_buf = heap_caps_malloc(needed_size + 1, MALLOC_CAP_SPIRAM);
	if (*str_buf == NULL) {
		return ESP_ERR_NO_MEM;
	}

	/* 生成最终字符串 */
	sprintf(*str_buf, "http://%u.%u.%u.%u/", ip1, ip2, ip3, ip4);

	return ESP_OK;
}

#if 1 // button
// 定义回调函数
void callback_1()
{
	printf("GPIO %d callback executed\n", GPIO_IO_NUM_1);
	show_prev_image(gyepd);
}
void callback_2()
{
	printf("GPIO %d callback executed\n", GPIO_IO_NUM_2);
	show_next_image(gyepd);
}
void callback_3()
{
	printf("GPIO %d callback executed\n", GPIO_IO_NUM_3);
	delete_current_image(gyepd);
}
void callback_4()
{
	printf("GPIO %d callback executed\n", GPIO_IO_NUM_4);
}

// 定义 GPIO 和对应的定时器与回调函数的数组
typedef struct {
	int gpio_num;
	esp_timer_handle_t timer_handle;
	callback_t callback;
} gpio_monitor_t;

gpio_monitor_t gpio_monitor[GPIO_NUM] = { { GPIO_IO_NUM_1, NULL, callback_1 },
					  { GPIO_IO_NUM_2, NULL, callback_2 },
					  { GPIO_IO_NUM_3, NULL, callback_3 },
					  { GPIO_IO_NUM_4, NULL, callback_4 } };

#if 0
// 定时器回调函数
static void timer_callback(void *arg)
{
	gpio_monitor_t *monitor = (gpio_monitor_t *)arg;
	int level = gpio_get_level(monitor->gpio_num);
	if (level == 0) { // 确认引脚仍为低电平
		monitor->callback();
	}
}

// GPIO 中断服务
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
	gpio_monitor_t *monitor = (gpio_monitor_t *)arg;
	int level = gpio_get_level(monitor->gpio_num);
	if (level == 0) { // 检测到低电平
		// 启动定时器
		esp_timer_start_once(monitor->timer_handle,
				     DEBOUNCE_TIME_MS * 1000);
	}
}
#endif

void bsp_gpio_initial(void)
{
	gpio_config_t gpiocfg_out_lcd = {};
	gpiocfg_out_lcd.intr_type = GPIO_INTR_DISABLE;
	gpiocfg_out_lcd.mode = GPIO_MODE_OUTPUT;
	gpiocfg_out_lcd.pin_bit_mask = (1ULL << PIN_SW3) | (1ULL << PIN_SW46);
	gpiocfg_out_lcd.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpiocfg_out_lcd.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&gpiocfg_out_lcd);

	/*gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_NEGEDGE, // 检测下降沿
		.mode = GPIO_MODE_INPUT, // 输入模式
		.pin_bit_mask =
			(1ULL << GPIO_IO_NUM_1) | (1ULL << GPIO_IO_NUM_2) |
			(1ULL << GPIO_IO_NUM_3) | (1ULL << GPIO_IO_NUM_4),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_ENABLE, // 启用上拉
	};
	gpio_config(&io_conf);

	// 创建定时器和中断服务
	for (int i = 0; i < GPIO_NUM; i++) {
		esp_timer_create_args_t timer_args = {
			.callback = timer_callback,
			.arg = &gpio_monitor[i],
			.dispatch_method = ESP_TIMER_TASK,
			.name = "gpio_timer"
		};
		esp_timer_create(&timer_args, &gpio_monitor[i].timer_handle);

		gpio_isr_handler_add(gpio_monitor[i].gpio_num, gpio_isr_handler,
				     &gpio_monitor[i]);
	}*/
}

#endif

// 返回图片数量
int scan_and_sort_images()
{
	image_count = 0;
	struct dirent *entry;
	DIR *dir = opendir("/sdcard");
	if (dir == NULL) {
		ESP_LOGE(TAG, "Failed to open directory.\n");
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strstr(entry->d_name, ".jpg")) {
			int num = atoi(entry->d_name); // 提取文件名中的整数部分
			if (num >= 0 && image_count < MAX_FILES) {
				image_numbers[image_count++] = num;
			}
		}
	}
	closedir(dir);

	// 排序图片编号
	qsort(image_numbers, image_count, sizeof(int),
	      (int (*)(const void *, const void *))strcmp);

	return image_count;
}

int get_file_num_from_index(int index)
{
	if (index < 0 || index >= image_count) {
		return -1;
	}
	return image_numbers[index];
}

int get_max_jpg_num(void)
{
	if (image_count == 0) {
		return -1;
	}
	return image_numbers[image_count - 1];
}

int set_current_image_number(int num)
{
	if (num < 0) {
		return -1;
	}

	current_image_number = num;
	return 0;
}

void show_next_image(YEPD *epd)
{
	scan_and_sort_images();

	if (image_count == 0) {
		ESP_LOGE(TAG, "No images to display.\n");
		return;
	}

	for (int i = 0; i < image_count; i++) {
		if (image_numbers[i] > current_image_number) {
			current_image_number = image_numbers[i];
			char filepath[64];
			snprintf(filepath, sizeof(filepath), "/sdcard/%d.jpg",
				 current_image_number);
			printf("Displaying: %s\n", filepath);
			display_jpg_numble(epd, current_image_number);
			return;
		}
	}

	// 如果超出范围，回到最小值
	current_image_number = image_numbers[0];
	char filepath[64];
	snprintf(filepath, sizeof(filepath), "/sdcard/%d.jpg",
		 current_image_number);
	printf("Displaying: %s (looped to first)\n", filepath);
	display_jpg_numble(epd, current_image_number);
}

void show_prev_image(YEPD *epd)
{
	scan_and_sort_images();

	if (image_count == 0) {
		printf("No images to display.\n");
		return;
	}

	for (int i = image_count - 1; i >= 0; i--) {
		if (image_numbers[i] < current_image_number) {
			current_image_number = image_numbers[i];
			char filepath[64];
			snprintf(filepath, sizeof(filepath), "/sdcard/%d.jpg",
				 current_image_number);
			printf("Displaying: %s\n", filepath);
			display_jpg_numble(epd, current_image_number);
			return;
		}
	}

	// 如果超出范围，回到最大值
	current_image_number = image_numbers[image_count - 1];
	char filepath[64];
	snprintf(filepath, sizeof(filepath), "/sdcard/%d.jpg",
		 current_image_number);
	printf("Displaying: %s (looped to last)\n", filepath);
	display_jpg_numble(epd, current_image_number);
}

void add_new_image(const char *new_image_path)
{
	scan_and_sort_images();

	int new_number =
		(image_count > 0) ? image_numbers[image_count - 1] + 1 : 1;
	char new_filepath[64];
	snprintf(new_filepath, sizeof(new_filepath), "/sdcard/%d.jpg",
		 new_number);

	// 假设 new_image_path 是上传文件的路径，进行文件拷贝
	if (rename(new_image_path, new_filepath) == 0) {
		printf("New image added: %s\n", new_filepath);
	} else {
		printf("Failed to add new image.\n");
	}
}

void delete_current_image()
{
	if (current_image_number < 0) {
		printf("No current image to delete.\n");
		return;
	}

	char filepath[64];
	snprintf(filepath, sizeof(filepath), "/sdcard/%d.jpg",
		 current_image_number);
	if (unlink(filepath) == 0) {
		printf("Deleted image: %s\n", filepath);
	} else {
		printf("Failed to delete image: %s\n", filepath);
		return;
	}

	// 删除后切换到编号更小的图片
	scan_and_sort_images();

	if (image_count == 0) {
		// 如果没有图片剩余，重置为无效值
		current_image_number = -1;
		printf("No images left.\n");
		display_palette(gyepd);
		return;
	}

	// 找到比当前编号小的文件，或者回到第一个文件
	for (int i = image_count - 1; i > 0; i--) {
		if (image_numbers[i] < current_image_number) {
			current_image_number = image_numbers[i];
			char filepath_next[64];
			snprintf(filepath_next, sizeof(filepath_next),
				 "/sdcard/%d.jpg", current_image_number);
			printf("Displaying: %s\n", filepath_next);
			display_jpg_numble(gyepd, current_image_number);
			return;
		}
	}

	// 如果没有比当前编号更大的图片，循环到编号最小的文件
	current_image_number = image_numbers[0];
	char filepath_first[64];
	snprintf(filepath_first, sizeof(filepath_first), "/sdcard/%d.jpg",
		 current_image_number);
	printf("Displaying: %s (looped to smallest)\n", filepath_first);
	display_jpg_numble(gyepd, current_image_number);
}
