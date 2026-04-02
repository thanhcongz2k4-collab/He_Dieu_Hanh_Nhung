#ifndef __OLED_H__
#define __OLED_H__

#include <stddef.h>
#include <stdint.h>

// Sử dụng SPL cho STM32F103
#include "oled_i2c.h"
#include "oled_fonts.h"

// #define FLIP_IMG

#define OLED_INCLUDE_FONT_11x18

#ifdef OLED_X_OFFSET
#define OLED_X_OFFSET_LOWER (OLED_X_OFFSET & 0x0F)
#define OLED_X_OFFSET_UPPER ((OLED_X_OFFSET >> 4) & 0x07)
#else
#define OLED_X_OFFSET_LOWER 0x00
#define OLED_X_OFFSET_UPPER 0x00
#endif

// SSD1306 OLED height in pixels
#ifndef OLED_HEIGHT
#define OLED_HEIGHT          64
#endif

// SSD1306 width in pixels
#ifndef OLED_WIDTH
#define OLED_WIDTH           128
#endif

#ifndef OLED_BUFFER_SIZE
#define OLED_BUFFER_SIZE   (OLED_WIDTH * OLED_HEIGHT / 8)
#endif

// Enumeration for screen colors
typedef enum {
    Black = 0x00, // Black color, no pixel
    White = 0x01  // Pixel is set. Color depends on OLED
} Oled_Color_e;

// Struct to store transformations
typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t Initialized;
    uint8_t DisplayOn;
} Oled_t;

typedef struct {
    uint8_t x;
    uint8_t y;
} Oled_Cursor_t;

typedef struct{
  uint8_t width;
  uint8_t height;
} Oled_StringSize_t;

// Procedure definitions
void Oled_Init(void);
void Oled_UpdateScreen(void);

void Oled_Fill(Oled_Color_e color);
Oled_Cursor_t Oled_GetCursor(void);
void Oled_SetCursor(uint8_t x, uint8_t y);
void Oled_DrawPixel(uint8_t x, uint8_t y, Oled_Color_e color);
Oled_StringSize_t Oled_GetStringSize(char *str, const GFXfont_t *font);
void Oled_WriteChar(char c, const GFXfont_t *font, Oled_Color_e color);
void Oled_WriteString(const char *str, const GFXfont_t *font, Oled_Color_e color);
void Oled_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);
void Oled_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);
void Oled_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);

#endif