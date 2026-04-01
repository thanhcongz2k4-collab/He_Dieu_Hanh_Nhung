#ifndef __ONE_WIRE__
#define __ONE_WIRE__
#include <stdint.h>

void OneWire_Init(void);
uint8_t OneWire_Reset(void);
void OneWire_WriteBit(uint8_t bit);
uint8_t OneWire_ReadBit(void);
void OneWire_WriteByte(uint8_t byte);
uint8_t OneWire_ReadByte(void);

#endif // __ONE_WIRE__