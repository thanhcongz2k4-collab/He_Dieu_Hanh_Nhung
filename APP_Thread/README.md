# Package `app` — RGB LED Controller (BeagleBone)

Ứng dụng điều khiển RGB LED chạy trên BeagleBone, gồm 3 task song song:
- **Task_Oled** — hiển thị giá trị RGB lên màn hình OLED SSD1306
- **Task_NRF_Receiver** — nhận dữ liệu RGB qua NRF24L01 (tạm thời stub)
- **MQTT_Task** — publish/subscribe giá trị RGB qua MQTT broker

---

## 1. Cấu trúc thư mục

```
buildroot/
└── package/
    ├── oled/                      # Thư viện OLED SSD1306
    │   ├── Config.in
    │   ├── oled.mk
    │   └── src/
    │       ├── oled/
    │       │   ├── oled.c
    │       │   └── oled.h
    │       ├── oled_i2c/
    │       │   ├── oled_i2c.c
    │       │   └── oled_i2c.h
    │       └── oled_fonts/
    │           ├── oled_fonts.c
    │           └── oled_fonts.h
    │
    ├── mqtt/                      # Thư viện wrapper paho-mqtt-c
    │   ├── Config.in
    │   ├── mqtt.mk
    │   └── src/
    │       ├── mqtt.h
    │       └── mqtt.c
    │
    ├── nrf24/
    │   ├── Config.in
    │   ├── nrf24.mk
    │   └── src/
    │       ├── nrf_spi/
    │       │   ├── nrf_spi.c
    │       │   └── nrf_spi.h
    │       └── nrf24l01/
    │           ├── nrf24l01.c
    │           └── nrf24l01.h
    │
    ├── rgb/                        
    │   ├── src/
    │   │   ├── pwm/
    │   │   │   ├── pwm.h
    │   │   │   └── pwm.c
    │   │   └── rgb_led/
    │   │       ├── rgb_led.h
    │   │       └── rgb_led.c
    │   ├── Config.in
    │   └── rgb.mk
    │
    └── app/                       # Ứng dụng chính
        ├── Config.in
        ├── app.mk
        └── src/
            ├── main.c
            └── all_header.h
```

---

## 2. Dependencies

| Package | Vai trò | Nguồn |
|---|---|---|
| `oled` | Thư viện điều khiển OLED SSD1306 qua I2C | local |
| `nrf24`| Thư viện điều khiển NRF24L01 qua SPI | local |
| `rgb`  | Thư viện điều khiển led RGB | local |
| `mqtt` | Wrapper mỏng cho paho-mqtt-c | local |
| `cjson` | Parse JSON payload MQTT | Buildroot built-in |
| `paho-mqtt-c` | MQTT client C library | Buildroot built-in |
| `pthread` | POSIX Threads | glibc (có sẵn trong toolchain) |
---

## 3. Buildroot config

### `package/app/Config.in`
```
config BR2_PACKAGE_APP
	bool "app"
	depends on BR2_USE_MMU
	depends on BR2_PACKAGE_OLED
	depends on BR2_PACKAGE_MQTT
	depends on BR2_PACKAGE_NRF24
	select BR2_PACKAGE_RGB
	select BR2_PACKAGE_CJSON   
	help
	  RGB LED controller application for BeagleBone.
	  Receives RGB data via NRF24L01, displays on OLED,
	  and publishes/subscribes via MQTT.

```

### `package/app/app.mk`
```makefile

APP_VERSION      = 1.0.0
APP_SITE         = $(TOPDIR)/package/app/src
APP_SITE_METHOD  = local

APP_LICENSE          = PROPRIETARY
APP_DEPENDENCIES = oled mqtt cjson

define APP_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(STAGING_DIR)/usr/include \
		$(@D)/main.c \
		-o $(@D)/app \
		-L$(STAGING_DIR)/usr/lib \
		-loled -lmqtt -lcjson -lpthread -lnrf24 -lrgb
endef

define APP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/app $(TARGET_DIR)/usr/bin/app
endef

$(eval $(generic-package))


```

---

## 4. Source code

### `src/all_header.h`
```c
#ifndef __ALL_HEADER_H__
#define __ALL_HEADER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <cjson/cJSON.h>
#include "oled.h"
#include "nrf24l01.h"
#include "rgb_led.h"
#include "mqtt.h"

#define DEVICE "/dev/btn"

#define MQTT_TOPIC_PUB  "BeagleBone/RGB/status"   // BBB gửi lên
#define MQTT_TOPIC_SUB  "BeagleBone/RGB/set"       // BBB nhận lệnh

#endif // __ALL_HEADER_H__

```

### `src/main.c`
```c
#include "all_header.h"

// Định nghĩa hàm msleep để ngủ theo đơn vị milliseconds
#define msleep(x) usleep((x) * 1000)
volatile int button_flag = 0;

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
    (void)parameter;
    int fd = open(DEVICE, O_RDONLY);
    if (fd < 0)
    {
        perror("Failed to open button device");
        return NULL;
    }

    while (1)
    {
        char buf[16];
        ssize_t bytes_read = read(fd, buf, sizeof(buf));
        if (bytes_read < 0)
        {
            perror("Failed to read button state");
            break;
        }
        if (bytes_read >= sizeof(buf))
            bytes_read = sizeof(buf) - 1;
        
        buf[bytes_read] = '\0';
        if(buf[0] == '1')
        {
            printf("Button pressed\n");
            button_flag = !button_flag;
        }
        msleep(100);
    }

    close(fd);
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
        if (button_flag) {
            msleep(100);
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
        if(!button_flag){
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
    if(!button_flag){
        printf("Button is pressed, ignoring MQTT message\n");
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


```

---
