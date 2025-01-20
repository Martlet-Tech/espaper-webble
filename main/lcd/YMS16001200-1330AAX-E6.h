
//=================================================================================================
//                   E2803 Driver for 13.3" Control Board
//
// File Name : EL133UF1.h
// Author : Electronic Design Dept. II
// Data : 2023.12.13
// Version : 1.1
// Copyright : E Ink Holdings Inc.
//=================================================================================================

#ifndef __EL133UF1_H__
#define __EL133UF1_H__

#include "esp_attr.h"
#include "yepd.h"

int EL133UF1_new_init(void);
int EL133UF1_new_update(void);
int EL133UF1_new_fill_index(uint8_t *index_buff);

#endif // #ifndef __EL133UF1_H__
