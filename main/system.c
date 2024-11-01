#include "system.h"
#include <esp_heap_caps.h>
#include "esp_log.h"

void show_ram_space(const char *position_string)
{
	ESP_LOGI("        RAM SPACE",
		 "Free heap : \t%d bytes, Largest block: \t%d \t@ %s",
		 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
		 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
		 position_string);
}
