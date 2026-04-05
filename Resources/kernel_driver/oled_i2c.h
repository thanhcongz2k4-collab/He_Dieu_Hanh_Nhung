#ifndef __OLED_I2C_H__
#define __OLED_I2C_H__
#include <stdlib.h>
#include <stdint.h>
#include "config.h"

#if defined(TEST_STM32)
#include "stm32f10x.h"

#define OLED_I2C_PORT        I2C2
#define OLED_I2C_ADDR        (0x3C << 1)

#else
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "oled_lib.h"

#endif

void Oled_I2C_Init(void);
void Oled_I2C_WriteCommand(uint8_t cmd);
void Oled_I2C_WriteData(uint8_t* buffer, size_t buff_size);

#endif // __OLED_I2C_H__