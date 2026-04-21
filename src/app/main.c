#include "all_header.h"

// Định nghĩa hàm msleep để ngủ theo đơn vị milliseconds
#define msleep(x) usleep((x) * 1000)
volatile uint8_t auto_mode = 0;

enum {
    IDLE_TASK_PRIORITY = 0,

    MQTT_TASK_PRIORITY = 40,
    OLED_TASK_PRIORITY = 50,
    NRF_RECEIVER_TASK_PRIORITY = 60
} TaskPriority;

// Địa chỉ 5 byte và kênh truyền cho NRF24L01
static const uint8_t addr[] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t channel = 40;

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
void* Task_Button(void *parameter);
void* Task_RGB_auto_switch_collor(void *parameter);

int main(void)
{
    pthread_attr_t attr;
    struct sched_param param;

    pthread_t t_oled, t_nrf, t_mqtt, t_button, t_rgb_auto;
    int rc;

    RGBLed_Init();

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* TODO: bật lại SCHED_FIFO khi chạy với CAP_SYS_NICE hoặc setuid root
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = OLED_TASK_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    */

    rc = pthread_create(&t_oled, &attr, Task_Oled, NULL);
    if (rc != 0) { printf("pthread_create Task_Oled failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_nrf, &attr, Task_NRF_Receiver, NULL);
    if (rc != 0) { printf("pthread_create Task_NRF failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_mqtt, &attr, MQTT_Task, NULL);
    if (rc != 0) { printf("pthread_create MQTT_Task failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_button, &attr, Task_Button, NULL);
    if (rc != 0) { printf("pthread_create Task_Button failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_rgb_auto, &attr, Task_RGB_auto_switch_collor, NULL);
    if (rc != 0) { printf("pthread_create Task_RGB_auto failed: %d\n", rc); return 1; }

    pthread_attr_destroy(&attr);

    while (1)
    {
        msleep(1000);
    }

    return 0;
}

void* Task_Button(void *parameter)
{
    static uint8_t status = 0;
    static uint8_t last_status = 0;
    Button_Config();

    while (1)
    {
        uint8_t is_pressed = 0;
        status = Button_Read();

        if(status && (status != last_status))
        {
            is_pressed = 1;
        }
        last_status = status;

        if(is_pressed)
        {
            auto_mode = !auto_mode;
        }
        msleep(50);
    }

    Button_Deinit();
    return NULL;
}

void* Task_Oled(void *parameter)
{
    RGB_Led local_rgb_led;


    Oled_Init();
    while (1)                   
    {
        msleep(20);

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

void* Task_RGB_auto_switch_collor(void *parameter)
{
    (void)parameter;
    float hue = 0.0f;

    while (1) {
        if (auto_mode == 0) {
            msleep(10);
            continue;
        }

        RGB_Led local_rgb_led = HSVtoRGB(hue, PWM_MAX_DUTY, PWM_MAX_DUTY);
        RGBLed_Show(local_rgb_led);        

        pthread_mutex_lock(&rgb_data_mutex);
        rgb_led = local_rgb_led;
        pthread_mutex_unlock(&rgb_data_mutex);

        hue += 1.0f;
        if (hue >= 360.0f) hue = 0.0f;

        msleep(100);
    }
    return NULL;
}

void* Task_NRF_Receiver(void *parameter)
{
    NRF_RX_Mode_Init(addr, channel);
    NRF_StartListening();
    while (1)
    {
        if(auto_mode == 1){
            msleep(100);
            continue;
        }
        if(NRF_DataReady())
        {
            
            RGB_Led local_rgb_led;

            uint16_t data[3];
            memset(data, 0, sizeof(data));      
            NRF_ReadData((uint8_t *)data, sizeof(data)); 

            printf("RGB to ESP32: %u %u %u\n", data[0], data[1], data[2]);
            local_rgb_led.red_value = data[0];
            local_rgb_led.green_value = data[1];
            local_rgb_led.blue_value = data[2];

            RGBLed_Show(local_rgb_led);

            pthread_mutex_lock(&rgb_data_mutex);
            rgb_led = local_rgb_led;
            pthread_mutex_unlock(&rgb_data_mutex);
        }
        msleep(100);
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

    while (MQTT_SubscribeTopic(MQTT_TOPIC_SUB) != MQTTCLIENT_SUCCESS)
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

            while (MQTT_SubscribeTopic(MQTT_TOPIC_SUB) != MQTTCLIENT_SUCCESS)
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
                publish_rc = MQTT_PublishMessage(MQTT_TOPIC_PUB, payload);
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

                        while (MQTT_SubscribeTopic(MQTT_TOPIC_SUB) != MQTTCLIENT_SUCCESS)
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

        msleep(500);
    }
    return NULL;
}


// Hàm callback xử lý tin nhắn MQTT nhận được
void On_MQTT_HandleMessage(const char* topic, const char* payload)
{
    if(auto_mode == 1){
        printf("Auto mode is enabled, ignoring MQTT message\n");
        return;
    }
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        printf("JSON parse error\n");
        return;
    }

    cJSON *red_value   = cJSON_GetObjectItem(root, "Red");
    cJSON *green_value = cJSON_GetObjectItem(root, "Green");
    cJSON *blue_value  = cJSON_GetObjectItem(root, "Blue");
    if (!cJSON_IsNumber(red_value) || !cJSON_IsNumber(green_value) || !cJSON_IsNumber(blue_value))
    {
        printf("Invalid JSON format: expected numeric values\n");
        cJSON_Delete(root);
        return;
    }

    int red   = cJSON_GetNumberValue(red_value);
    int green = cJSON_GetNumberValue(green_value);
    int blue  = cJSON_GetNumberValue(blue_value);

    RGB_Led local_rgb_led = { .red_value = red, .green_value = green, .blue_value = blue };
    printf("Received MQTT message on topic '%s': Red=%d, Green=%d, Blue=%d\n",
           topic, red, green, blue);
    RGBLed_Show(local_rgb_led);

    pthread_mutex_lock(&rgb_data_mutex);

    rgb_led = local_rgb_led;
    pthread_mutex_unlock(&rgb_data_mutex);

    cJSON_Delete(root);
}


