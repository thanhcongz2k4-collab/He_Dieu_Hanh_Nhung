#include "oled_i2c.h"

#if defined(TEST_STM32)
#define OLED_TIMEOUT 	1e4

#define WAIT_FLAG(FLAG)\
	IIC_OLED_TIMEOUT = OLED_TIMEOUT;\
	while((FLAG) && (IIC_OLED_TIMEOUT--));
	
int IIC_OLED_TIMEOUT = 0;


void Oled_Open(void) 
{
    GPIO_InitTypeDef GPIO_InitStruct;
    I2C_InitTypeDef I2C_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    if (OLED_I2C_PORT == I2C1) 
    {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
        GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    } 
    else if (OLED_I2C_PORT == I2C2) 
    {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
        GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    }
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    I2C_DeInit(OLED_I2C_PORT);
    I2C_InitStruct.I2C_ClockSpeed = 400000;
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_Ack = I2C_Ack_Disable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(OLED_I2C_PORT, &I2C_InitStruct);

    I2C_Cmd(OLED_I2C_PORT, ENABLE);
}

// Send a byte to the command register
void Oled_WriteCommand(uint8_t cmd) 
{
    // Sử dụng SPL thay cho HAL
    // Gửi byte lệnh qua I2C2
    // OLED_I2C_ADDR là địa chỉ 7 bit đã dịch trái 1 bit
    // 0x00 là control byte cho lệnh
    while(I2C_GetFlagStatus(OLED_I2C_PORT, I2C_FLAG_BUSY));

    I2C_GenerateSTART(OLED_I2C_PORT, ENABLE);
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_MODE_SELECT));
    I2C_Send7bitAddress(OLED_I2C_PORT, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
    I2C_SendData(OLED_I2C_PORT, 0x00); // Control byte: Co=0, D/C#=0
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));
    I2C_SendData(OLED_I2C_PORT, cmd);
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));
    I2C_GenerateSTOP(OLED_I2C_PORT, ENABLE);
}

// Send data
void Oled_WriteData(uint8_t* buffer, size_t buff_size) 
{
    while(I2C_GetFlagStatus(OLED_I2C_PORT, I2C_FLAG_BUSY));

    I2C_GenerateSTART(OLED_I2C_PORT, ENABLE);
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_MODE_SELECT));
    I2C_Send7bitAddress(OLED_I2C_PORT, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
    I2C_SendData(OLED_I2C_PORT, 0x40); // Control byte: Co=0, D/C#=1
    WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));
    for (size_t i = 0; i < buff_size; i++) {
        I2C_SendData(OLED_I2C_PORT, buffer[i]);
        WAIT_FLAG (!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));
    }
    I2C_GenerateSTOP(OLED_I2C_PORT, ENABLE);
}

#else

#define OLED_DEVICE "/dev/oled"

static int fd = -1;

void Oled_Open(void) 
{
    fd = open(OLED_DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("open %s", OLED_DEVICE);
        return;
    }
}

void Oled_WriteCommand(uint8_t cmd) 
{
    if (fd < 0) return;

    uint8_t buf[2] = {0x00, cmd};
    if (write(fd, buf, 2) < 0) {
        perror("write command");
    }
}

void Oled_WriteData(uint8_t* buffer, size_t buff_size) 
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
#endif