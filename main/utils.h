/**
 * @file utils.h
 * @author zhaitao (zhaitao.as@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-01
 * 
 * @copyright zhaitao.as@outlook.com (c) 2024
 * 
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#define GPIO_LOW 0
#define GPIO_HIGH 1

void show_ram_space(const char *position_string);
void delayms(unsigned int delayTime);
void delay_ms(unsigned int delayTime);
void delayus(unsigned int delayTime);
void delay_us(unsigned int delayTime);
void safe_free(int **ptr);
void swap_pixels(unsigned char *a, unsigned char *b);
int get_max_int_in_array(int *arr, int size);
void generate_random_password(char *password, size_t length);
#endif