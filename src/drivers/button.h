#ifndef __BUTTON__
#define __BUTTON__
#include <stdlib.h>
#include <stdint.h>

#define DEVICE "/dev/btn"

extern inline uint8_t Button_Config(void);
extern inline uint8_t Button_Read(void);
extern inline void Button_Deinit(void);

#endif