#ifndef __OLED_FONTS_H__
#define __OLED_FONTS_H__
#include <stdint.h>


#define DEFAULT_FONT  Serif_plain_10

typedef struct {
  uint16_t bitmapOffset; // Vị trí bắt đầu trong mảng bitmap
  uint8_t  width;        // Chiều rộng ký tự (px)
  uint8_t  height;       // Chiều cao ký tự (px)
  uint8_t  xAdvance;     // Khoảng cách con trỏ sau khi in
  int8_t   xOffset;      // Dịch trái/phải
  int8_t   yOffset;      // Dịch lên/xuống
} GFXglyph_t;

typedef struct {
  uint8_t  *bitmap;      // trỏ đến mảng bitmap[]
  GFXglyph_t *glyph;       // trỏ đến mảng glyph[]
  uint8_t   first;       // ASCII đầu tiên
  uint8_t   last;        // ASCII cuối
  uint8_t   yAdvance;    // khoảng cách dòng
} GFXfont_t;

extern const GFXfont_t Serif_plain_10;

#endif // __OLED_FONTS_H__
