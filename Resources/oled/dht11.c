#include "dht11.h"

void DHT11_Open(void)
{
  
}

DHT11_Data_t DHT11_Read(void)
{
  DHT11_Data_t data = {0, 0};
  // Giả lập đọc dữ liệu từ cảm biến DHT11
  data.temperature = 25; // Nhiệt độ giả lập
  data.humidity = 60;    // Độ ẩm giả lập
  return data;
}