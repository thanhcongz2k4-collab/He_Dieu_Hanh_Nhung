#ifndef __RGBLED_PWM__
#define __RGBLED_PWM__

#include "stm32f10x.h"                  // Device header

typedef struct 
{
	uint16_t red_value;
	uint16_t green_value;
	uint16_t blue_value;
	
} RGB_Led;

void RGBLed_PWM_Open(void);
void RGBLed_PWM_SetDuty(RGB_Led rgb_led);


#endif