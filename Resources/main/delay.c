
#include "delay.h"

void delay_ms(uint32_t time_delay)
{
	vTaskDelay(pdMS_TO_TICKS(time_delay));
}
