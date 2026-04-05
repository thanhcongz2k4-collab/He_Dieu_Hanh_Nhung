#include "rgb_led.h"

void RGBLed_Init(void)
{
  PWM_Config(RED_PWM_CHANNEL);
  PWM_Config(GREEN_PWM_CHANNEL);
  PWM_Config(BLUE_PWM_CHANNEL);
}


void RGBLed_Show(RGB_Led  rgb_led)
{
	PWM_SetDuty(RED_PWM_CHANNEL, rgb_led.red_value);
	PWM_SetDuty(GREEN_PWM_CHANNEL, rgb_led.green_value);
	PWM_SetDuty(BLUE_PWM_CHANNEL, rgb_led.blue_value);
}

// Hàm HSV . RGB với max=1000
RGB_Led  HSVtoRGB(float h, int s, int v) 
{
	RGB_Led 	rgb;
	
  float hh, p, q, t, ff;
  int i;
  float S = s / (float)PWM_MAX_DUTY;
  float V = v / (float)PWM_MAX_DUTY;

  if (s <= 0) {
    rgb.red_value = rgb.green_value = rgb.blue_value = v;
    return rgb;
  }

  hh = h / 60.0;
  i = (int)hh;
  ff = hh - i;
  p = V * (1.0 - S);
  q = V * (1.0 - (S * ff));
  t = V * (1.0 - (S * (1.0 - ff)));

  switch (i) {
    case 0: rgb.red_value = V * PWM_MAX_DUTY; rgb.green_value = t * PWM_MAX_DUTY; rgb.blue_value = p * PWM_MAX_DUTY; break;
    case 1: rgb.red_value = q * PWM_MAX_DUTY; rgb.green_value = V * PWM_MAX_DUTY; rgb.blue_value = p * PWM_MAX_DUTY; break;
    case 2: rgb.red_value = p * PWM_MAX_DUTY; rgb.green_value = V * PWM_MAX_DUTY; rgb.blue_value = t * PWM_MAX_DUTY; break;
    case 3: rgb.red_value = p * PWM_MAX_DUTY; rgb.green_value = q * PWM_MAX_DUTY; rgb.blue_value = V * PWM_MAX_DUTY; break;
    case 4: rgb.red_value = t * PWM_MAX_DUTY; rgb.green_value = p * PWM_MAX_DUTY; rgb.blue_value = V * PWM_MAX_DUTY; break;
    default:rgb.red_value = V * PWM_MAX_DUTY; rgb.green_value = p * PWM_MAX_DUTY; rgb.blue_value = q * PWM_MAX_DUTY; break;
  }
	
	return rgb;
}