#ifndef __OLED_I2C_H__
#define __OLED_I2C_H__
#include "stm32f10x.h"
#include <stdlib.h>
#include <stdint.h>

#ifndef OLED_I2C_PORT
#define OLED_I2C_PORT        I2C2
#endif

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR        (0x3C << 1)
#endif

void I2Cx_Init(void);
void Oled_WriteCommand(uint8_t byte);
void Oled_WriteData(uint8_t* buffer, size_t buff_size);

#endif // __OLED_I2C_H__