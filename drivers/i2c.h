#ifndef __I2C_H__
#define __I2C_H__
#include <stdlib.h>
#include <stdint.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

void I2C_Config(void);
void I2C_WriteCommand(uint8_t cmd);
void I2C_WriteData(uint8_t* buffer, size_t buff_size);

#endif // __I2C_H__