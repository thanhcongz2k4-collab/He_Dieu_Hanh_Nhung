#include "all_header.h"

enum {
    OLED_TASK_PRIORITY,
    NRF_RECEIVER_TASK_PRIORITY
};

// Địa chỉ 5 byte và kênh truyền cho NRF24L01
const uint8_t addr[] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
const uint8_t channel = 40;

// Struct điều khiển RGB; khởi tạo mức duty trung bình
RGB_Led  rgb_led;

void Task_Oled(void *parameter);
void Task_NRF_Receiver(void *parameter);

int main(void)
{
    xTaskCreate(Task_Oled, "OLED Task", 256, NULL, OLED_TASK_PRIORITY, NULL);
    xTaskCreate(Task_NRF_Receiver, "NRF Receiver Task", 256, NULL, NRF_RECEIVER_TASK_PRIORITY, NULL);
    vTaskStartScheduler();

    while (1) {
        // Main loop
    }
}

void Task_Oled(void *parameter)
{
    Oled_Init();
    while (1) {
        Oled_Fill(Black);

        char str[32] = "RGB Value";

        Oled_StringSize_t str_size = Oled_GetStringSize(str, &DEFAULT_FONT);

        Oled_FillRectangle(0, 0, OLED_WIDTH, str_size.height + 4, White);

        Oled_SetCursor((OLED_WIDTH - str_size.width) / 2, str_size.height + 2);
        Oled_WriteString(str, &DEFAULT_FONT, Black);

        const uint8_t text_delta_y = 13;
        uint8_t text_x = 5;
        uint8_t text_y = str_size.height + 20;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Red Value:     %d", rgb_led.red_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);
        
        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Green Value: %d", rgb_led.green_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Blue Value:    %d", rgb_led.blue_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        Oled_UpdateScreen();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Task_NRF_Receiver(void *parameter)
{
    RGBLed_Init();

    NRF_RX_Mode_Init(addr, channel);
    NRF_StartListening();
    while (1) 
    {
        if(NRF_DataReady())
        {
            // Đọc đúng sizeof(rgb_led) byte từ FIFO RX
            NRF_ReadData((uint8_t *)(&rgb_led), sizeof(rgb_led));

            // Hiển thị màu nhận được trên RGB LED
            RGBLed_Show(rgb_led);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}