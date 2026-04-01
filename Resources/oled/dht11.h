#ifndef __DHT11__
#define __DHT11__
#include <stdint.h>

typedef struct
{
  uint8_t temperature;
  uint8_t humidity;
}DHT11_Data_t;


void DHT11_Open(void);
DHT11_Data_t DHT11_Read(void);

#endif // __DHT11__