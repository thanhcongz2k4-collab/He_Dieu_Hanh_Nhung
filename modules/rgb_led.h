#ifndef __RGB_LED__
#define __RGB_LED__
#include "pwm.h"


#define RED_PWM_CHANNEL 1
#define GREEN_PWM_CHANNEL 2
#define BLUE_PWM_CHANNEL 3


typedef struct 
{
	uint16_t red_value;
	uint16_t green_value;
	uint16_t blue_value;
	
} RGB_Led;

void RGBLed_Init(void);
void RGBLed_Show(RGB_Led  rgb_led);
RGB_Led 	HSVtoRGB(float h, int s, int v);

#endif