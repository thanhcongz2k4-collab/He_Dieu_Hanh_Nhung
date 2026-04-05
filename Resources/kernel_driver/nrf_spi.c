/*
================================================================================
 Module: spi1.c
 Mô tả: Cấu hình SPI1 (Master, 8-bit, CPOL=0, CPHA=1edge, NSS mềm) và
		cung cấp hàm truyền/nhận 1 từ dữ liệu (8/16 bit theo cấu hình) đồng bộ.

 Chân mặc định:
 - PA5: SCK, PA7: MOSI (AF_PP), PA6: MISO (Input Pull-up)
 - CE, CSN: GPIO output push-pull dùng điều khiển NRF24L01

 Ghi chú:
 - Tốc độ SPI = f_PCLK2 / Prescaler. Với Prescaler_8 và PCLK2=72MHz, f_SPI=9MHz.
 - Nếu thiết bị ngoại vi yêu cầu CPOL/CPHA khác, cần điều chỉnh cho phù hợp.
================================================================================
*/
#include "nrf_spi.h"

#if !defined(TEST_STM32)
	
void NRF_Set_CE(uint8_t value)
{

}

void NRF_Set_CSN(uint8_t value)
{

}

void NRF_SPI_Config(void)
{
	// Implementation for non-STM32 platforms
}

uint16_t NRF_SPI_Transfer(uint16_t data)
{
	// Implementation for non-STM32 platforms
	return 0;
}

#else

void NRF_SPI_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	SPI_InitTypeDef  SPI_InitStructure;

	// Bật clock cho GPIOA và SPI1
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1,  ENABLE);

	// PA5 -> SCK, PA7 -> MOSI (Alternate Function Push-Pull)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
    
	// PA6 -> MISO (Input Pull-up)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// Cấu hình SPI1: Master, 8-bit, CPOL=0, CPHA=1edge, NSS mềm, tốc độ chia 8
	SPI_InitStructure.SPI_Direction                 = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode                      = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize                  = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL                      = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA                      = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS                       = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler         = SPI_BaudRatePrescaler_8; 
	SPI_InitStructure.SPI_FirstBit                  = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial             = 7;
	SPI_Init(SPI1, &SPI_InitStructure);
	SPI_Cmd(SPI1, ENABLE);

	// CE, CSN làm GPIO output điều khiển mô-đun NRF
	GPIO_InitStructure.GPIO_Pin = CE_PIN | CSN_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// Mặc định không phát (CE=0), không chọn SPI slave (CSN=1)
	NRF_CE_LOW();
	NRF_CSN_HIGH();
}

uint16_t NRF_SPI_Transfer(uint16_t data)
{
	// Chờ TXE=1 -> bộ đệm truyền rỗng
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
	// Ghi dữ liệu vào thanh ghi truyền -> bắt đầu clock
	SPI_I2S_SendData(SPI1, data);
	// Chờ RXNE=1 -> có dữ liệu nhận về (SPI luôn đồng bộ 2 chiều)
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
	// Đọc dữ liệu nhận và trả về
	return SPI_I2S_ReceiveData(SPI1);
}

#endif