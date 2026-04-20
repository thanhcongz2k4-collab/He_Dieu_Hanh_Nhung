#include "spi.h"

int nrf_fd = -1;

void SPI_Set_CE(uint8_t value)
{

}

void SPI_Set_CSN(uint8_t value)
{

}

void SPI_Config(void)
{
    nrf_fd = open("/dev/nrf24", O_RDWR);
    NRF_CE_LOW();
    NRF_CSN_HIGH();
}

uint8_t SPI_Transfer(uint8_t data)
{
    uint8_t rx = 0;
    if (write(nrf_fd, &data, 1) < 0) return 0;
    if (read(nrf_fd,  &rx,   1) < 0) return 0;
    return rx;
}
