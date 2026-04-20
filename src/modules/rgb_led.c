#include "rgb_led.h"

/* ------------------------------------------------------------------ */
/* Khởi tạo 3 kênh PWM                                                */
/* ------------------------------------------------------------------ */
void RGBLed_Init(void)
{
    PWM_Config(RED_PWM_CHANNEL);
    PWM_Config(GREEN_PWM_CHANNEL);
    PWM_Config(BLUE_PWM_CHANNEL);
}

/* ------------------------------------------------------------------ */
/* Hiển thị màu                                                        */
/* ------------------------------------------------------------------ */
void RGBLed_Show(RGB_Led rgb_led)
{
    PWM_SetDuty(RED_PWM_CHANNEL,   rgb_led.red_value);
    PWM_SetDuty(GREEN_PWM_CHANNEL, rgb_led.green_value);
    PWM_SetDuty(BLUE_PWM_CHANNEL,  rgb_led.blue_value);
}

/* ------------------------------------------------------------------ */
/* Tắt LED                                                             */
/* ------------------------------------------------------------------ */
void RGBLed_Off(void)
{
    RGBLed_Show(RGB_OFF);
}

/* ------------------------------------------------------------------ */
/* Chuyển đổi HSV → RGB                                               */
/* h: 0.0 ~ 360.0                                                      */
/* s: 0 ~ PWM_MAX_DUTY (1000)                                         */
/* v: 0 ~ PWM_MAX_DUTY (1000)                                         */
/* ------------------------------------------------------------------ */
RGB_Led HSVtoRGB(float h, int s, int v)
{
    RGB_Led rgb;
    float   hh, p, q, t, ff;
    int     i;
    float   S = s / (float)PWM_MAX_DUTY;
    float   V = v / (float)PWM_MAX_DUTY;

    if (s <= 0) {
        rgb.red_value = rgb.green_value = rgb.blue_value = (uint16_t)v;
        return rgb;
    }

    if (h >= 360.0f) h = 0.0f;

    hh = h / 60.0f;
    i  = (int)hh;
    ff = hh - i;

    p = V * (1.0f - S);
    q = V * (1.0f - (S * ff));
    t = V * (1.0f - (S * (1.0f - ff)));

    switch (i) {
        case 0:
            rgb.red_value   = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(t * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(p * PWM_MAX_DUTY);
            break;
        case 1:
            rgb.red_value   = (uint16_t)(q * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(p * PWM_MAX_DUTY);
            break;
        case 2:
            rgb.red_value   = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(t * PWM_MAX_DUTY);
            break;
        case 3:
            rgb.red_value   = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(q * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(V * PWM_MAX_DUTY);
            break;
        case 4:
            rgb.red_value   = (uint16_t)(t * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(V * PWM_MAX_DUTY);
            break;
        default:
            rgb.red_value   = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(q * PWM_MAX_DUTY);
            break;
    }

    return rgb;
}