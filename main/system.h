#ifndef __SYSTEM_H
#define __SYSTEM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

void show_ram_space(const char *position_string);

#endif