/*
 * @file       delay library
 * @board      STM32F10x
 * @author     Tong Sy Tan
 * @date       Sun, 09/02/2025
*/

#ifndef __DELAY__
#define __DELAY__
#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f10x.h"                  // Device header
#include "FreeRTOS.h"
#include "task.h"

void delay_ms(uint32_t time_delay);

#ifdef __cplusplus
}
#endif
#endif