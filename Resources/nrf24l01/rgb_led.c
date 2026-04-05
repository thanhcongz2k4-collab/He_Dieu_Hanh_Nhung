/*
================================================================================
 Module: rgb_led.c
 Mô tả: Điều khiển LED RGB bằng PWM trên TIM2 (kênh 1-3) và GPIO ở chế độ AF_PP.
        Cung cấp hàm khởi tạo và hàm hiển thị theo mức duty (0..PERIOD).

 Tham số cấu hình:
 - PERIOD (mặc định 1000): tương ứng độ phân giải PWM (duty từ 0..PERIOD).

 Lưu ý:
 - Các kênh PWM ánh xạ: CCR1 . BLUE, CCR2 . GREEN, CCR3 . RED.
 - Tần số PWM phụ thuộc Prescaler và PERIOD. 
 - Điều chỉnh Prescaler/PERIOD để đạt tần số PWM thích hợp (thường >200 Hz tránh nhấp nháy).
================================================================================
*/
#include "rgb_led.h"

void RGBLed_Init(void)
{
  RGBLed_PWM_Config();
}


void RGBLed_Show(RGB_Led  rgb_led)
{
	RGBLed_PWM_SetDuty(rgb_led);
}


// Hàm HSV . RGB với max=1000
RGB_Led 	HSVtoRGB(float h, int s, int v) 
{
	RGB_Led 	rgb;
	
  float hh, p, q, t, ff;
  int i;
  float S = s / (float)RGBLED_PWM_MAX_DUTY;
  float V = v / (float)RGBLED_PWM_MAX_DUTY;

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
    case 0: rgb.red_value = V * RGBLED_PWM_MAX_DUTY; rgb.green_value = t * RGBLED_PWM_MAX_DUTY; rgb.blue_value = p * RGBLED_PWM_MAX_DUTY; break;
    case 1: rgb.red_value = q * RGBLED_PWM_MAX_DUTY; rgb.green_value = V * RGBLED_PWM_MAX_DUTY; rgb.blue_value = p * RGBLED_PWM_MAX_DUTY; break;
    case 2: rgb.red_value = p * RGBLED_PWM_MAX_DUTY; rgb.green_value = V * RGBLED_PWM_MAX_DUTY; rgb.blue_value = t * RGBLED_PWM_MAX_DUTY; break;
    case 3: rgb.red_value = p * RGBLED_PWM_MAX_DUTY; rgb.green_value = q * RGBLED_PWM_MAX_DUTY; rgb.blue_value = V * RGBLED_PWM_MAX_DUTY; break;
    case 4: rgb.red_value = t * RGBLED_PWM_MAX_DUTY; rgb.green_value = p * RGBLED_PWM_MAX_DUTY; rgb.blue_value = V * RGBLED_PWM_MAX_DUTY; break;
    default:rgb.red_value = V * RGBLED_PWM_MAX_DUTY; rgb.green_value = p * RGBLED_PWM_MAX_DUTY; rgb.blue_value = q * RGBLED_PWM_MAX_DUTY; break;
  }
	
	return rgb;
}