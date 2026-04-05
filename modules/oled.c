#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>  // For memcpy
#include <math.h>

// Screenbuffer
static uint8_t oled_buffer[OLED_BUFFER_SIZE];

// Screen object
static Oled_t Oled;

/* Initialize the oled screen */
void Oled_Init(void) 
{
    uint8_t mux_ratio = 0x3F;
    uint8_t com_pins_cfg = 0x12;

    if (OLED_HEIGHT == 32) {
        mux_ratio = 0x1F;
        com_pins_cfg = 0x02;
    }

    I2C_Config();
    for(volatile int i=0; i<0xfffff; i++);

    I2C_WriteCommand(0xAE);
    I2C_WriteCommand(0x20); // memory addressing mode
    I2C_WriteCommand(0x00); // horizontal
    I2C_WriteCommand(0xB0);
    I2C_WriteCommand(0xC8);
    I2C_WriteCommand(0x00);
    I2C_WriteCommand(0x10);
    I2C_WriteCommand(0x40);
    I2C_WriteCommand(0x81);
    I2C_WriteCommand(0x7F);
    I2C_WriteCommand(0xA1);
    I2C_WriteCommand(0xA6);
    I2C_WriteCommand(0xA8);
    I2C_WriteCommand(mux_ratio);
    I2C_WriteCommand(0xA4);
    I2C_WriteCommand(0xD3);
    I2C_WriteCommand(0x00);
    I2C_WriteCommand(0xD5);
    I2C_WriteCommand(0x80);
    I2C_WriteCommand(0xD9);
    I2C_WriteCommand(0xF1);
    I2C_WriteCommand(0xDA);
    I2C_WriteCommand(com_pins_cfg);
    I2C_WriteCommand(0xDB);
    I2C_WriteCommand(0x40);
    I2C_WriteCommand(0x8D);
    I2C_WriteCommand(0x14);
    I2C_WriteCommand(0xAF);
    
    // Clear screen
    Oled_Fill(Black);
    
    // Flush buffer to screen
    Oled_UpdateScreen();
    
    // Set default values for screen object
    Oled.CurrentX = 0;
    Oled.CurrentY = 0;
    
    Oled.Initialized = 1;
}

/* Fill the whole screen with the given color */
void Oled_Fill(Oled_Color_e color) 
{
    memset(oled_buffer, (color == Black) ? 0x00 : 0xFF, sizeof(oled_buffer));
}

/* Write the screenbuffer with changed to the screen */
void Oled_UpdateScreen(void) 
{
    // Write data to each page of RAM. Number of pages
    // depends on the screen height:
    //
    //  * 32px   ==  4 pages
    //  * 64px   ==  8 pages
    //  * 128px  ==  16 pages
    for(uint8_t page = 0; page < OLED_HEIGHT/8; page++) 
    {
        I2C_WriteCommand(0xB0 + page); // Set page address
        I2C_WriteCommand(0x00 + OLED_X_OFFSET_LOWER); // Set lower column address
        I2C_WriteCommand(0x10 + OLED_X_OFFSET_UPPER); // Set higher column address
        I2C_WriteData(&oled_buffer[OLED_WIDTH*page], OLED_WIDTH);
    }
}

/*
 * Draw one pixel in the screenbuffer
 * X => X Coordinate
 * Y => Y Coordinate
 * color => Pixel color
 */
void Oled_DrawPixel(uint8_t x, uint8_t y, Oled_Color_e color) 
{
    
#ifdef FLIP_IMG
    y = OLED_HEIGHT - 1 - y;
    x = OLED_WIDTH - 1 - x;
#endif
   
    if(x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        // Don't write outside the buffer
        return;
    }
   
    // Draw in the right color
    if(color == White) {
        oled_buffer[x + (y / 8) * OLED_WIDTH] |= 1 << (y % 8);
    } else { 
        oled_buffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
    }
}

Oled_StringSize_t Oled_GetStringSize(char *str, const GFXfont_t *font)
{
    Oled_StringSize_t str_size = {0, 0};
    int8_t y_offset = 0;
    
    while(*str)
    {
        if (*str < font->first || *str > font->last) 
				{
					str_size.width = 0;
					str_size.height = 0;
					return str_size;
				}

        GFXglyph_t *glyph = &font->glyph[*str - font->first];
        
        if(abs(glyph->height - abs(glyph->yOffset)))
        { 
            if(y_offset < abs(glyph->height - abs(glyph->yOffset))) y_offset = abs(glyph->height - abs(glyph->yOffset));
        }
        if(str_size.height < glyph->height) str_size.height = glyph->height;
        str_size.width += glyph->xAdvance;
        str++;
    }
    str_size.height += y_offset;
    return str_size;
}

/*
 * Draw 1 char to the screen buffer
 * ch       => char om weg te schrijven
 * Font     => Font waarmee we gaan schrijven
 * color    => Black or White
 */
void Oled_WriteChar(char c, const GFXfont_t *font, Oled_Color_e color) {
    if (c < font->first || c > font->last) return;

    GFXglyph_t *glyph = &font->glyph[c - font->first];
    uint8_t  *bitmap = font->bitmap;

    uint16_t bo = glyph->bitmapOffset;
    uint8_t  w  = glyph->width;
    uint8_t  h  = glyph->height;
    int8_t   xo = glyph->xOffset;
    int8_t   yo = glyph->yOffset;

    int16_t x = Oled.CurrentX;
    int16_t y = Oled.CurrentY;

    uint8_t bit = 0, bits = bitmap[bo];

    for (uint8_t yy = 0; yy < h; yy++) {
        for (uint8_t xx = 0; xx < w; xx++) {
            if (bit >= 8) {
                bits = bitmap[++bo];
                bit  = 0;
            }
            if (bits & 0x80) {
                uint8_t cx = x + xo + xx;
                uint8_t cy = y + yo + yy;
                
            #ifdef FLIP_IMG
                uint8_t tx = OLED_WIDTH - 1 - cx;
                uint8_t ty = OLED_HEIGHT - 1 - cy;
                
            #else
                uint8_t tx = cx;
                uint8_t ty = cy;
            #endif
   
                
                // Toggle color
                if(color == White && (oled_buffer[tx + (ty / 8) * OLED_WIDTH] & (1 << (ty % 8)))) 
                {
                    color = Black;
                } 
                else if(color == Black && !(oled_buffer[tx + (ty / 8) * OLED_WIDTH] & (1 << (ty % 8)))) 
                {
                    color = White;
                }
                Oled_DrawPixel(cx, cy, color);
            }
            bits <<= 1;
            bit++;
        }
    }

    // Tự động dịch con trỏ sang phải
    Oled.CurrentX += glyph->xAdvance;
}

/* Write full string to screenbuffer */
void Oled_WriteString(const char *str, const GFXfont_t *font, Oled_Color_e color) 
{
    while (*str) {
        if (*str == '\n') 
        {
            Oled.CurrentY += font->yAdvance;
            Oled.CurrentX  = 0;
        } 
        else if (*str >= font->first && *str <= font->last) 
        {
            Oled_WriteChar(*str, font, color);
        }
        str++;
    }
}


Oled_Cursor_t Oled_GetCursor(void)
{
    Oled_Cursor_t cursor = {Oled.CurrentX, Oled.CurrentY};
    return cursor;
}

/* Position the cursor */
void Oled_SetCursor(uint8_t x, uint8_t y) 
{
    Oled.CurrentX = x;
    Oled.CurrentY = y;
}

/* Draw line by Bresenhem's algorithm */
void Oled_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    
    Oled_DrawPixel(x2, y2, color);

    while((x1 != x2) || (y1 != y2)) {
        Oled_DrawPixel(x1, y1, color);
        error2 = error * 2;
        if(error2 > -deltaY) {
            error -= deltaY;
            x1 += signX;
        }
        
        if(error2 < deltaX) {
            error += deltaX;
            y1 += signY;
        }
    }
    return;
}

/* Draw a rectangle */
void Oled_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    Oled_Line(x1,y1,x2,y1,color);
    Oled_Line(x2,y1,x2,y2,color);
    Oled_Line(x2,y2,x1,y2,color);
    Oled_Line(x1,y2,x1,y1,color);

    return;
}

/* Draw a filled rectangle */
void Oled_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    uint8_t x_start = ((x1<=x2) ? x1 : x2);
    uint8_t x_end   = ((x1<=x2) ? x2 : x1);
    uint8_t y_start = ((y1<=y2) ? y1 : y2);
    uint8_t y_end   = ((y1<=y2) ? y2 : y1);

    for (uint8_t y= y_start; (y<= y_end)&&(y<OLED_HEIGHT); y++) {
        for (uint8_t x= x_start; (x<= x_end)&&(x<OLED_WIDTH); x++) {
            Oled_DrawPixel(x, y, color);
        }
    }
    return;
}
