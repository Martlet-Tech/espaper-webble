
#include "img_prcs.h"
#include "util.h"
#include "EL133UF1.h"
#include "file.h"

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "jpeg_decoder.h"

void rotate_90_counterclockwise_rgb888(unsigned char *image, int width,
				       int height);
void stuckiDither(uint8_t *image, uint8_t *output_index, int image_width,
		  int image_height);

void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s);
void reorder_array(uint8_t *array, int width, int height);

esp_err_t display_jpg_file(const char *filename)
{
	const char *TAG = "display_jpg_file";
	esp_err_t ret;

	uint32_t DST_FRAME_SIZE = EPD_FRAME_SIZE;
	uint8_t *dst_image_buffer_m = NULL;
	uint8_t *dst_image_buffer_s = NULL;
	uint8_t *org_image_buffer = NULL;
	uint8_t *index_buffer = NULL;

	uint8_t *jpeg_buffer;
	uint16_t w = EPD_WIDTH;
	uint16_t h = EPD_HEIGHT;
	uint32_t file_size;
	uint32_t rgb_buff_size;

	ESP_LOGI(TAG, "start, file: %s", filename);
	show_ram_space("start of display_jpg_file");

	rgb_buff_size = EPD_WIDTH * EPD_HEIGHT * 3;
	org_image_buffer =
		(uint8_t *)heap_caps_malloc(rgb_buff_size, MALLOC_CAP_SPIRAM);

	jpeg_buffer = SD_MMC_ReadFileToPsram(filename, &file_size);
	if (jpeg_buffer == NULL) {
		free(org_image_buffer);
		return ESP_FAIL;
	}

	esp_jpeg_image_cfg_t jpeg_cfg = { .indata = (uint8_t *)jpeg_buffer,
					  .indata_size = file_size,
					  .outbuf = org_image_buffer,
					  .outbuf_size = rgb_buff_size,
					  .out_format =
						  JPEG_IMAGE_FORMAT_RGB888,
					  .out_scale = JPEG_IMAGE_SCALE_0,
					  .flags = {
						  .swap_color_bytes = 0,
					  } };
	esp_jpeg_image_output_t outimg;

	ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "jpg file decode failed");
		free(org_image_buffer);
		return ESP_FAIL;
	} else {
		ESP_LOGI(TAG, "jpg file w: %d, h: %d", outimg.width,
			 outimg.height);
	}
	show_ram_space("before free jpeg_buffer");
	ESP_LOGI(TAG, "free jpg file ram");
	free(jpeg_buffer);
	show_ram_space("after free jpeg_buffer");

	/*if ((outimg.width == EPD_HEIGHT) && (outimg.height == EPD_WIDTH)) {
		ESP_LOGI(TAG, "image need to rotate 90d");
		rotate_90_counterclockwise_rgb888(org_image_buffer,
						  outimg.width, outimg.height);
	}*/

	show_ram_space("before malloc index_buffer");
	index_buffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT,
						   MALLOC_CAP_SPIRAM);
	show_ram_space("after malloc index_buffer");

	stuckiDither((uint8_t *)org_image_buffer, (uint8_t *)index_buffer, w,
		     h);

	free(org_image_buffer);
	show_ram_space("after free org_image_buffer");

	reorder_array(index_buffer, outimg.width, outimg.height);

	dst_image_buffer_m =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	dst_image_buffer_s =
		(uint8_t *)heap_caps_malloc(DST_FRAME_SIZE, MALLOC_CAP_SPIRAM);
	palette_index_to_E6_data(index_buffer, dst_image_buffer_m,
				 dst_image_buffer_s);
	free(index_buffer);
	ESP_LOGI(TAG, "取模完成");

	EL133UF1_Init();
	EL133UF1_DisplayFrame(dst_image_buffer_m, dst_image_buffer_s);
	EL133UF1_Sleep();
	EL133UF1_Deinit();
	free(dst_image_buffer_m);
	free(dst_image_buffer_s);
	ESP_LOGI(TAG, "显示完成");
	show_ram_space("end of display_jpg_file");

	return ESP_OK;
}

// 交换两个像素，大小为 3 字节 (RGB)
void swap_pixels(unsigned char *a, unsigned char *b)
{
	unsigned char temp[3];
	memcpy(temp, a, 3);
	memcpy(a, b, 3);
	memcpy(b, temp, 3);
}

#define PALETTE_SIZE 6

const uint8_t palette[PALETTE_SIZE][3] = {
	{ 0x00, 0x00, 0x00 }, //BLACK 0,index = 0
	{ 0xFF, 0xFF, 0xFF }, //WHITE 1,index = 1
	{ 0xFF, 0xFF, 0x00 }, //YELLOW 2,index = 2
	{ 0xFF, 0x00, 0x00 }, //RED 3,index = 3
	{ 0x00, 0x00, 0xFF }, //BLUE 4,index = 5
	{ 0x00, 0xFF, 0x00 }, //GREEN 5,index = 6
};

uint8_t plus_truncate_uchar(uint8_t a, int b)
{
	if ((a & 0xff) + b < 0) {
		return 0;
	} else if ((a & 0xff) + b > 255) {
		return (uint8_t)255;
	} else {
		return (uint8_t)(a + b);
	}
}

uint8_t FindNearestColor(uint8_t *pixel_rgb)
{
	int minDistanceSquared = 255 * 255 + 255 * 255 + 255 * 255 + 1;
	int bestIndex = 0;
	for (int i = 0; i < PALETTE_SIZE; i++) {
		int Rdiff = ((int)pixel_rgb[0] & 0xff) -
			    ((int)palette[i][0] & 0xff);
		int Gdiff = ((int)pixel_rgb[1] & 0xff) -
			    ((int)palette[i][1] & 0xff);
		int Bdiff = ((int)pixel_rgb[2] & 0xff) -
			    ((int)palette[i][2] & 0xff);
		int distanceSquared =
			Rdiff * Rdiff + Gdiff * Gdiff + Bdiff * Bdiff;
		if (distanceSquared < minDistanceSquared) {
			minDistanceSquared = distanceSquared;
			bestIndex = i;
		}
	}
	return (uint8_t)bestIndex;
}

void stuckiDither(uint8_t *image, uint8_t *output_index, int image_width,
		  int image_height)
{
	for (int y = 0; y < image_height; y++) {
		// 每行结束后让出 CPU
		if (y % 10 == 0) { // 可以尝试让出 CPU 的频率，比如每 10 行
			taskYIELD(); // 或者 vTaskDelay(1)
		}

		for (int x = 0; x < image_width; x++) {
			//taskYIELD(); // 或者 vTaskDelay(1) 来让出 CPU

			uint8_t *currentPixel =
				image + (y * image_width + x) * 3;
			uint8_t index = FindNearestColor(currentPixel);
			output_index[y * image_width + x] = index;

			for (int i = 0; i < 3; i++) { //RGB
				//计算误差
				int error = (currentPixel[i] & 0xff) -
					    (palette[index][i] & 0xff);
				//扩散误差
				int pixel_offset = (y * image_width + x) * 3;
				if (x + 1 < image_width) {
					image[pixel_offset + 3 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 3 +
							      i],
							(error * 8) / 42);
				}
				if (x + 2 < image_width) {
					image[pixel_offset + 6 + i] =
						plus_truncate_uchar(
							image[pixel_offset + 6 +
							      i],
							(error * 4) / 42);
				}
				if (y + 1 < image_height) {
					if (x - 2 > 0) {
						image[pixel_offset +
						      image_width * 3 - 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 -
								      6 + i],
								(error * 2) /
									42);
					}
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 3 - 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 -
								      3 + i],
								(error * 4) /
									42);
					}
					image[pixel_offset + image_width * 3 +
					      i] =
						plus_truncate_uchar(
							image[pixel_offset +
							      image_width * 3 +
							      i],
							(error * 8) / 42);
					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      3 + i],
								(error * 4) /
									42);
					}
					if (x + 2 < image_width) {
						image[pixel_offset +
						      image_width * 3 + 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      3 +
								      6 + i],
								(error * 2) /
									42);
					}
				}
				if (y + 2 < image_height) {
					if (x - 2 > 0) {
						image[pixel_offset +
						      image_width * 6 - 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 -
								      6 + i],
								(error * 1) /
									42);
					}
					if (x - 1 > 0) {
						image[pixel_offset +
						      image_width * 6 - 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 -
								      3 + i],
								(error * 2) /
									42);
					}
					image[pixel_offset + image_width * 6 +
					      i] =
						plus_truncate_uchar(
							image[pixel_offset +
							      image_width * 6 +
							      i],
							(error * 4) / 42);
					if (x + 1 < image_width) {
						image[pixel_offset +
						      image_width * 6 + 3 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      3 + i],
								(error * 2) /
									42);
					}
					if (x + 2 < image_width) {
						image[pixel_offset +
						      image_width * 6 + 6 + i] =
							plus_truncate_uchar(
								image[pixel_offset +
								      image_width *
									      6 +
								      6 + i],
								(error * 1) /
									42);
					}
				}
			}
		}
	}
}

void palette_index_to_E6_data(uint8_t *index_buffer, uint8_t *dst_m,
			      uint8_t *dst_s)
{
	uint8_t temp = 0;
	uint8_t index;
	int pixel_count = 0;
	for (int j = 0; j < EPD_HEIGHT; j++) {
		for (int i = 0; i < EPD_WIDTH / 2; i++) {
			temp <<= 4;
			index = index_buffer[i + j * EPD_WIDTH];
			if (index >= 4)
				index++;
			temp |= index;
			pixel_count++;
			if (pixel_count >= 2) {
				pixel_count = 0;
				*dst_m = temp;
				temp = 0;
				dst_m++;
			}
		}

		for (int i = EPD_WIDTH / 2; i < EPD_WIDTH; i++) {
			temp <<= 4;
			index = index_buffer[i + j * EPD_WIDTH];
			if (index >= 4)
				index++;
			temp |= index;
			pixel_count++;
			if (pixel_count >= 2) {
				pixel_count = 0;
				*dst_s = temp;
				temp = 0;
				dst_s++;
			}
		}
	}
}

void reorder_array(uint8_t *array, int width, int height)
{
	uint8_t *temp =
		(uint8_t *)malloc(width * height * sizeof(uint8_t)); // 临时缓存
	memset(temp, 0, width * height * sizeof(uint8_t));

	// 遍历每一列
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			// 原始数组中 (y, x) 的元素在新数组中的位置应该是：
			// 新数组中从左下角开始遍历，列优先
			temp[x * height + (height - 1 - y)] =
				array[y * width + x];
		}
	}

	// 将重组后的数据拷贝回原数组
	memcpy(array, temp, width * height * sizeof(uint8_t));

	free(temp); // 释放临时缓存
}

#if 0
// 在原地进行 90 度旋转（顺时针）
void rotate_90_clockwise_rgb888(unsigned char *image, int width, int height)
{
	int block_size = 16; // Adjust this according to your memory limits
	uint8_t temp[block_size * 3]; // Temporary buffer for row storage

	for (int y = 0; y < height; y += block_size) {
		for (int x = 0; x < width; x += block_size) {
			for (int i = 0; i < block_size && y + i < height; i++) {
				// Store a row of pixels into the temp buffer
				for (int j = 0; j < block_size && x + j < width;
				     j++) {
					// Calculate original and new positions
					int orig_idx =
						((y + i) * width + (x + j)) * 3;
					int rotated_idx =
						((x + j) * height +
						 (height - 1 - (y + i))) *
						3;

					// Store in temp buffer (copy one row at a time)
					temp[j * 3] = image[rotated_idx];
					temp[j * 3 + 1] =
						image[rotated_idx + 1];
					temp[j * 3 + 2] =
						image[rotated_idx + 2];

					// Copy back the rotated pixels
					image[rotated_idx] = image[orig_idx];
					image[rotated_idx + 1] =
						image[orig_idx + 1];
					image[rotated_idx + 2] =
						image[orig_idx + 2];
				}
			}
		}
	}
}

// 逆时针旋转90度
void rotate_90_counterclockwise_rgb888(unsigned char *image, int width,
				       int height)
{
	int block_size = 16; // Adjust this according to your memory limits
	uint8_t temp[block_size * 3]; // Temporary buffer for row storage

	for (int y = 0; y < height; y += block_size) {
		for (int x = 0; x < width; x += block_size) {
			for (int i = 0; i < block_size && y + i < height; i++) {
				// Store a row of pixels into the temp buffer
				for (int j = 0; j < block_size && x + j < width;
				     j++) {
					// Calculate original and new positions
					int orig_idx =
						((y + i) * width + (x + j)) * 3;
					int rotated_idx =
						((x + j) * height +
						 (height - 1 - (y + i))) *
						3;

					// Store in temp buffer (copy one row at a time)
					temp[j * 3] = image[rotated_idx];
					temp[j * 3 + 1] =
						image[rotated_idx + 1];
					temp[j * 3 + 2] =
						image[rotated_idx + 2];

					// Copy back the rotated pixels
					image[rotated_idx] = image[orig_idx];
					image[rotated_idx + 1] =
						image[orig_idx + 1];
					image[rotated_idx + 2] =
						image[orig_idx + 2];
				}
			}
		}
	}
}

#endif