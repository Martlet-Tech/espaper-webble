/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __GATTS_TABLE_H__
#define __GATTS_TABLE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADV_NAME_DEFAULT "YES_EPD_GATTS"

/* Attributes State Machine */
enum {
	IDX_SVC,
	IDX_CHAR_A,
	IDX_CHAR_VAL_A,
	IDX_CHAR_CFG_A,

	IDX_CHAR_B,
	IDX_CHAR_VAL_B,

	IDX_CHAR_C,
	IDX_CHAR_VAL_C,

	IDX_CHAR_D,
	IDX_CHAR_VAL_D,

	IDX_CHAR_E,
	IDX_CHAR_VAL_E,

	HRS_IDX_NB,
};

extern char saved_custom_name[64];
extern size_t custom_name_size;

void gatts_main(void);

#endif