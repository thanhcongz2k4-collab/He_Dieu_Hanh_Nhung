#ifndef __RGB_LED__
#define __RGB_LED__
#ifdef __cplusplus
extern "C"{
#endif
#include "stm32f10x.h"                  // Device header

#include "rgbled_pwm.h"                // Sử dụng driver PWM chung cho RGB LED

void RGBLed_Init(void);
void RGBLed_Show(RGB_Led  rgb_led);
RGB_Led 	HSVtoRGB(float h, int s, int v);


#ifdef __cplusplus
}
#endif
#endif