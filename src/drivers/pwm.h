#ifndef __PWM__
#define __PWM__

#include <stdlib.h>
#include <stdint.h>

#define PWM_MAX_DUTY 1000

/* Sysfs paths cho từng channel */
#define PWM_CH1_PATH  "/sys/class/leds/rgb:red/brightness"
#define PWM_CH2_PATH  "/sys/class/leds/rgb:green/brightness"
#define PWM_CH3_PATH  "/sys/class/leds/rgb:blue/brightness"

void     PWM_Config(int channel);
void     PWM_SetDuty(int channel, uint16_t duty);

#endif /* __PWM__ */
