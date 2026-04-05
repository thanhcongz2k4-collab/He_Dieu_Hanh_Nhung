#include "all_header.h"

#define msleep(x) usleep((x) * 1000)

enum {
    IDLE_TASK_PRIORITY = 0,

    MQTT_TASK_PRIORITY = 40,
    OLED_TASK_PRIORITY = 50,
    NRF_RECEIVER_TASK_PRIORITY = 60
} TaskPriority;

// Địa chỉ 5 byte và kênh truyền cho NRF24L01
static const uint8_t addr[] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t channel = 40;

// Struct điều khiển RGB; khởi tạo mức duty trung bình
static RGB_Led  rgb_led;
static RGB_Led  last_rgb_led;

static int RGBLed_Equals(const RGB_Led *left, const RGB_Led *right)
{
    return (left->red_value == right->red_value) &&
           (left->green_value == right->green_value) &&
           (left->blue_value == right->blue_value);
}

static pthread_mutex_t rgb_data_mutex = PTHREAD_MUTEX_INITIALIZER;

void* MQTT_Task(void *parameter);
void* Task_Oled(void *parameter);
void* Task_NRF_Receiver(void *parameter);

int main(void)
{   
    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); /* tự dọn khi xong */

    param.sched_priority = OLED_TASK_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(NULL, &attr, Task_Oled, NULL);  
    
    param.sched_priority = NRF_RECEIVER_TASK_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(NULL, &attr, Task_NRF_Receiver, NULL);

    param.sched_priority = MQTT_TASK_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(NULL, &attr, MQTT_Task, NULL);

    pthread_attr_destroy(&attr);

    while (1) 
    {
        msleep(1000);
    }

    return 0;
}

void* Task_Oled(void *parameter)
{
    RGB_Led local_rgb_led;

    Oled_Init();
    while (1) 
    {
        msleep(20);

        // Copy dữ liệu RGB từ biến toàn cục sang biến cục bộ để tránh tranh chấp dữ liệu
        pthread_mutex_lock(&rgb_data_mutex);
        local_rgb_led = rgb_led;
        pthread_mutex_unlock(&rgb_data_mutex);

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
        snprintf(str, sizeof(str), "Red Value:     %d", local_rgb_led.red_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);
        
        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Green Value: %d", local_rgb_led.green_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Blue Value:    %d", local_rgb_led.blue_value);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        Oled_UpdateScreen();
    }
    return NULL;
}

void* Task_NRF_Receiver(void *parameter)
{
    RGBLed_Init();

    NRF_RX_Mode_Init(addr, channel);
    NRF_StartListening();
    while (1) 
    {
        msleep(100);
        if(NRF_DataReady())
        {
            RGB_Led local_rgb_led;

            // Đọc đúng sizeof(rgb_led) byte từ FIFO RX
            NRF_ReadData((uint8_t *)(&local_rgb_led), sizeof(local_rgb_led));

            // Hiển thị màu nhận được trên RGB LED
            RGBLed_Show(local_rgb_led);

            // Cập nhật giá trị RGB toàn cục để hiển thị trên OLED
            pthread_mutex_lock(&rgb_data_mutex);
            rgb_led = local_rgb_led;
            pthread_mutex_unlock(&rgb_data_mutex);
        }
    }
    return NULL;
}

void On_MQTT_HandleMessage(const char* topic, const char* payload);

void* MQTT_Task(void *parameter)
{
    const uint32_t reconnect_delay_ms = 2000;
    const uint32_t publish_retry_delay_ms = 500;
    (void)parameter;

    while (MQTT_Init(On_MQTT_HandleMessage) != MQTTCLIENT_SUCCESS)
    {
        printf("MQTT init failed, retrying...\n");
        msleep(reconnect_delay_ms);
    }

    while (MQTT_SubscribeTopic(MQTT_TOPIC) != MQTTCLIENT_SUCCESS)
    {
        printf("MQTT subscribe failed, retrying...\n");

        while (MQTT_Reconnect() != MQTTCLIENT_SUCCESS)
        {
            printf("MQTT reconnect failed, retrying...\n");
            msleep(reconnect_delay_ms);
        }

        msleep(reconnect_delay_ms);
    }

    while (1) 
    {
        RGB_Led local_rgb_led;

        pthread_mutex_lock(&rgb_data_mutex);
        local_rgb_led = rgb_led;
        pthread_mutex_unlock(&rgb_data_mutex);

        if (!MQTT_IsConnected())
        {
            while (MQTT_Reconnect() != MQTTCLIENT_SUCCESS)
            {
                printf("MQTT reconnect failed, retrying...\n");
                msleep(reconnect_delay_ms);
            }

            while (MQTT_SubscribeTopic(MQTT_TOPIC) != MQTTCLIENT_SUCCESS)
            {
                printf("MQTT subscribe failed after reconnect, retrying...\n");
                msleep(reconnect_delay_ms);
            }
        }

        if(!RGBLed_Equals(&last_rgb_led, &local_rgb_led))
        {
            char payload[128];
            int publish_rc;

            snprintf(payload, sizeof(payload), "{\"Red\": %d, \"Green\": %d, \"Blue\": %d}", local_rgb_led.red_value, local_rgb_led.green_value, local_rgb_led.blue_value);

            do
            {
                publish_rc = MQTT_PublishMessage(MQTT_TOPIC, payload);
                if (publish_rc != MQTTCLIENT_SUCCESS)
                {
                    printf("MQTT publish failed, retrying...\n");

                    if (!MQTT_IsConnected())
                    {
                        while (MQTT_Reconnect() != MQTTCLIENT_SUCCESS)
                        {
                            printf("MQTT reconnect failed, retrying...\n");
                            msleep(reconnect_delay_ms);
                        }

                        while (MQTT_SubscribeTopic(MQTT_TOPIC) != MQTTCLIENT_SUCCESS)
                        {
                            printf("MQTT subscribe failed after reconnect, retrying...\n");
                            msleep(reconnect_delay_ms);
                        }
                    }

                    msleep(publish_retry_delay_ms);
                }
            } while (publish_rc != MQTTCLIENT_SUCCESS);

            last_rgb_led = local_rgb_led;
        }

        msleep(1000);
    }
    return NULL;
}

void On_MQTT_HandleMessage(const char* topic, const char* payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) 
    {
        printf("JSON parse error\n");
        return;
    }

    cJSON *red_value = cJSON_GetObjectItem(root, "Red");
    cJSON *green_value = cJSON_GetObjectItem(root, "Green");
    cJSON *blue_value = cJSON_GetObjectItem(root, "Blue");
    if (!cJSON_IsNumber(red_value) || !cJSON_IsNumber(green_value) || !cJSON_IsNumber(blue_value)) 
    {
        printf("Invalid JSON format: expected numeric values\n");
        cJSON_Delete(root);
        return;
    }

    // Lấy giá trị RGB từ JSON
    int red = cJSON_GetNumberValue(red_value);
    int green = cJSON_GetNumberValue(green_value);
    int blue = cJSON_GetNumberValue(blue_value);

    // Cập nhật giá trị RGB toàn cục
    pthread_mutex_lock(&rgb_data_mutex);
    rgb_led.red_value = red;
    rgb_led.green_value = green;
    rgb_led.blue_value = blue;

    last_rgb_led = rgb_led; // Cập nhật last_rgb_led để tránh gửi lại cùng giá trị
    pthread_mutex_unlock(&rgb_data_mutex);

    cJSON_Delete(root);
}