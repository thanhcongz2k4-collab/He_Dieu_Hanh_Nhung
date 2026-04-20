#ifndef __RGB_LED__
#define __RGB_LED__

#include "pwm.h"

#define RED_PWM_CHANNEL   1
#define GREEN_PWM_CHANNEL 2
#define BLUE_PWM_CHANNEL  3

typedef struct {
    uint16_t red_value;
    uint16_t green_value;
    uint16_t blue_value;
} RGB_Led;

void    RGBLed_Init(void);
void    RGBLed_Show(RGB_Led rgb_led);
void    RGBLed_Off(void);

RGB_Led HSVtoRGB(float h, int s, int v);

/* Một số màu định sẵn (scale 0..PWM_MAX_DUTY) */
#define RGB_RED     (RGB_Led){ PWM_MAX_DUTY, 0,              0            }
#define RGB_GREEN   (RGB_Led){ 0,            PWM_MAX_DUTY,   0            }
#define RGB_BLUE    (RGB_Led){ 0,            0,              PWM_MAX_DUTY }
#define RGB_YELLOW  (RGB_Led){ PWM_MAX_DUTY, PWM_MAX_DUTY,   0            }
#define RGB_CYAN    (RGB_Led){ 0,            PWM_MAX_DUTY,   PWM_MAX_DUTY }
#define RGB_MAGENTA (RGB_Led){ PWM_MAX_DUTY, 0,              PWM_MAX_DUTY }
#define RGB_WHITE   (RGB_Led){ PWM_MAX_DUTY, PWM_MAX_DUTY,   PWM_MAX_DUTY }
#define RGB_OFF     (RGB_Led){ 0,            0,              0            }

#endif /* __RGB_LED__ */