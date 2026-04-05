#ifndef __NRF_SPI__
#define __NRF_SPI__
#ifdef __cplusplus
extern "C"{
#endif
#include <stdlib.h>
#include <stdint.h>
#include "config.h"

#if defined(TEST_STM32)
#include "stm32f10x.h"                  // Device header

#define CE_PIN 			GPIO_Pin_3
#define CSN_PIN 		GPIO_Pin_4


#define NRF_CE_HIGH()   GPIOA->BSRR = CE_PIN
#define NRF_CE_LOW()    GPIOA->BRR  = CE_PIN
#define NRF_CSN_HIGH()  GPIOA->BSRR = CSN_PIN
#define NRF_CSN_LOW()   GPIOA->BRR  = CSN_PIN
#else 

#define NRF_CE_HIGH()   NRF_Set_CE(1)
#define NRF_CE_LOW()    NRF_Set_CE(0)
#define NRF_CSN_HIGH()  NRF_Set_CSN(1)
#define NRF_CSN_LOW()   NRF_Set_CSN(0)

void NRF_Set_CE(uint8_t value);
void NRF_Set_CSN(uint8_t value);

#endif

void NRF_SPI_Config(void);
uint16_t NRF_SPI_Transfer(uint16_t data);

#ifdef __cplusplus
}
#endif
#endif