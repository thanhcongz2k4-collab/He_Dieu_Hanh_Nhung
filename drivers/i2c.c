#include "i2c.h"

#include "oled_lib.h"

#define I2C_DEVICE "/dev/oled"

static int fd = -1;

void I2C_Config(void) 
{
    fd = open(I2C_DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("open %s", I2C_DEVICE);
        return;
    }
}

void I2C_WriteCommand(uint8_t cmd) 
{
    if (fd < 0) return;

    uint8_t buf[2] = {0x00, cmd};
    if (write(fd, buf, 2) < 0) {
        perror("write command");
    }
}

void I2C_WriteData(uint8_t* buffer, size_t buff_size) 
{
    if (fd < 0) return;

    uint8_t tmp[buff_size + 1];
    tmp[0] = 0x40;

    for (size_t i = 0; i < buff_size; i++) {
        tmp[i + 1] = buffer[i];
    }

    if (write(fd, tmp, buff_size + 1) < 0) {
        perror("write data");
    }
}