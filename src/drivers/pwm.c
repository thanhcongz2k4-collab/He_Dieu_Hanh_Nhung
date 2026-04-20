#include "pwm.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* File descriptors cho 3 channel, mở 1 lần */
static int pwm_fd[4] = { -1, -1, -1, -1 }; /* index 1..3 */

static const char *pwm_path(int channel)
{
    switch (channel) {
        case 1: return PWM_CH1_PATH;
        case 2: return PWM_CH2_PATH;
        case 3: return PWM_CH3_PATH;
        default: return NULL;
    }
}

/* Mở file sysfs, sẵn sàng ghi */
void PWM_Config(int channel)
{
    const char *path = pwm_path(channel);
    if (!path) {
        fprintf(stderr, "PWM_Config: invalid channel %d\n", channel);
        return;
    }

    if (pwm_fd[channel] >= 0)
        return; /* đã mở rồi */

    pwm_fd[channel] = open(path, O_WRONLY);
    if (pwm_fd[channel] < 0)
        perror(path);
}

/* Ghi duty cycle vào sysfs
 * duty: 0 ~ PWM_MAX_DUTY (1000) → map sang 0 ~ 255 */
void PWM_SetDuty(int channel, uint16_t duty)
{
    char buf[8];
    int  len;
    int  brightness;

    if (channel < 1 || channel > 3 || pwm_fd[channel] < 0)
        return;

    if (duty > PWM_MAX_DUTY)
        duty = PWM_MAX_DUTY;

    /* Scale: 0..1000 → 0..255 */
    brightness = (int)((uint32_t)duty * 255 / PWM_MAX_DUTY);

    len = snprintf(buf, sizeof(buf), "%d", brightness);
    lseek(pwm_fd[channel], 0, SEEK_SET);
    (void)write(pwm_fd[channel], buf, len);
}