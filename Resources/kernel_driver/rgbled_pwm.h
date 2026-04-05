#ifndef __RGBLED_PWM__
#define __RGBLED_PWM__
#include <stdlib.h>
#include <stdint.h>
#include "config.h"

#if defined(TEST_STM32)

#include "stm32f10x.h"                  // Device header

#endif

#define RGBLED_PWM_MAX_DUTY 1000

typedef struct 
{
	uint16_t red_value;
	uint16_t green_value;
	uint16_t blue_value;
	
} RGB_Led;

void RGBLed_PWM_Config(void);
void RGBLed_PWM_SetDuty(RGB_Led rgb_led);


#endif