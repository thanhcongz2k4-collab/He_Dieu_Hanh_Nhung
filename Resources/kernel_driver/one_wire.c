#include "one_wire.h"

void OneWire_Init(void)
{
  
}

uint8_t OneWire_Reset(void)
{
  // Giả lập reset bus 1-Wire
  // Trong thực tế, bạn sẽ sử dụng các hàm của thư viện phần cứng để điều khiển GPIO
  return 1; // Trả về 1 nếu có thiết bị phản hồi, 0 nếu không
}

void OneWire_WriteBit(uint8_t bit)
{
  // Giả lập ghi một bit ra bus 1-Wire
  // Trong thực tế, bạn sẽ sử dụng các hàm của thư viện phần cứng để điều khiển GPIO
}

uint8_t OneWire_ReadBit(void)
{
  // Giả lập đọc một bit từ bus 1-Wire
  // Trong thực tế, bạn sẽ sử dụng các hàm của thư viện phần cứng để đọc trạng thái GPIO
  return 0; // Trả về giá trị bit đọc được (0 hoặc 1)
}

void OneWire_WriteByte(uint8_t byte)
{
  for (int i = 0; i < 8; i++) {
    OneWire_WriteBit((byte >> i) & 0x01);
  }
}

uint8_t OneWire_ReadByte(void)
{
  uint8_t byte = 0;
  for (int i = 0; i < 8; i++) {
    byte |= (OneWire_ReadBit() << i);
  }
  return byte;
}

