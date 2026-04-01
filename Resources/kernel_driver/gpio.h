#ifndef __GPIO__
#define __GPIO__
#include <stdint.h>

void GPIO_Open(void);
void GPIO_Write(uint8_t value);
uint8_t GPIO_Read(void);  

#endif // __GPIO__