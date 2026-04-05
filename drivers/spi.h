#ifndef __SPI__
#define __SPI__
#include <stdlib.h>
#include <stdint.h>

#define SPI_CE_HIGH()   SPI_Set_CE(1)
#define SPI_CE_LOW()    SPI_Set_CE(0)
#define SPI_CSN_HIGH()  SPI_Set_CSN(1)
#define SPI_CSN_LOW()   SPI_Set_CSN(0)

void SPI_Set_CE(uint8_t value);
void SPI_Set_CSN(uint8_t value);

void SPI_Config(void);
uint16_t SPI_Transfer(uint16_t data);

#endif