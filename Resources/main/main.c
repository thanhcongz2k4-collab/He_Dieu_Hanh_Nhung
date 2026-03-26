#include "all_header.h"

int main(void)
{
    Oled_Init();
    Oled_Fill(Black);
    
    uint8_t *str = "Hello, World!";
    Oled_StringSize_t str_size = Oled_GetStringSize(str, &DEFAULT_FONT);
    Oled_DrawRectangle((OLED_WIDTH - str_size.width) / 2 - 5, (OLED_HEIGHT - str_size.height) / 2 - 5, 
                       (OLED_WIDTH + str_size.width) / 2 + 5, (OLED_HEIGHT + str_size.height) / 2 + 5, White);
                       
    Oled_SetCursor((OLED_WIDTH - str_size.width) / 2, (OLED_HEIGHT + str_size.height) / 2);
    Oled_WriteString(str, &DEFAULT_FONT, White);
    Oled_UpdateScreen();

    while (1) {
        // Main loop
    }
}