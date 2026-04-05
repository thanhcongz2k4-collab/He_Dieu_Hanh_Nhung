#ifndef __CONFIG_H__
#define __CONFIG_H__

// #define TEST_STM32

#if defined(TEST_STM32)
#define FUNC_POINTER

#include "FreeRTOS.h"
#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

#else 
#define FUNC_POINTER *

#define delay_ms(x) usleep((x) * 1000)

#endif

#endif // __CONFIG_H__