#ifndef __PWM__
#define __PWM__
#include <stdlib.h>
#include <stdint.h>

#define PWM_MAX_DUTY 1000

void PWM_Config(int channel);
void PWM_SetDuty(int channel, uint16_t duty);


#endif