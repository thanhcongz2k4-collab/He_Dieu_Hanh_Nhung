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
    select BR2_PACKAGE_CJSON
    help
      RGB LED controller application for BeagleBone.
      Receives RGB data via NRF24L01, displays on OLED,
      and publishes/subscribes via MQTT.
```

### `package/app/app.mk`
```makefile
################################################################################
#
# app
#
################################################################################

APP_VERSION      = 1.0.0
APP_SITE         = $(TOPDIR)/package/app/src
APP_SITE_METHOD  = local

APP_LICENSE          = PROPRIETARY
APP_DEPENDENCIES     = oled mqtt cjson

define APP_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) \
        -I$(STAGING_DIR)/usr/include \
        $(@D)/main.c \
        -o $(@D)/app \
        -L$(STAGING_DIR)/usr/lib \
        -loled -lmqtt -lcjson -lpthread
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
// #include "nrf24l01.h"   // TODO: chưa có package
// #include "rgb_led.h"    // TODO: chưa có package
#include "mqtt.h"

#define MQTT_TOPIC  "BeagleBone/RGB"

#endif // __ALL_HEADER_H__
```

### `src/main.c`
```c
#include "all_header.h"

#define msleep(x) usleep((x) * 1000)

enum {
    IDLE_TASK_PRIORITY         = 0,
    MQTT_TASK_PRIORITY         = 40,
    OLED_TASK_PRIORITY         = 50,
    NRF_RECEIVER_TASK_PRIORITY = 60
} TaskPriority;

static const uint8_t addr[]  = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t channel = 40;

static pthread_mutex_t rgb_data_mutex = PTHREAD_MUTEX_INITIALIZER;

void* MQTT_Task(void *parameter);
void* Task_Oled(void *parameter);
void* Task_NRF_Receiver(void *parameter);

int main(void)
{
    pthread_attr_t attr;
    pthread_t t_oled, t_nrf, t_mqtt;
    int rc;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /*
     * TODO: bật lại SCHED_FIFO khi deploy production
     * Yêu cầu CAP_SYS_NICE hoặc chạy qua: chrt -f 1 /usr/bin/app
     *
     * struct sched_param param;
     * pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
     * pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
     * param.sched_priority = OLED_TASK_PRIORITY;
     * pthread_attr_setschedparam(&attr, &param);
     */

    rc = pthread_create(&t_oled, &attr, Task_Oled, NULL);
    if (rc != 0) { printf("pthread_create Task_Oled failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_nrf, &attr, Task_NRF_Receiver, NULL);
    if (rc != 0) { printf("pthread_create Task_NRF failed: %d\n", rc); return 1; }

    rc = pthread_create(&t_mqtt, &attr, MQTT_Task, NULL);
    if (rc != 0) { printf("pthread_create MQTT_Task failed: %d\n", rc); return 1; }

    pthread_attr_destroy(&attr);

    while (1) { msleep(1000); }

    return 0;
}

void* Task_Oled(void *parameter)
{
    uint8_t local_value = 0;

    Oled_Init();
    while (1)
    {
        msleep(20);

        pthread_mutex_lock(&rgb_data_mutex);
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
        snprintf(str, sizeof(str), "Red Value:     %d", local_value++);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Green Value: %d", local_value++);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        text_y += text_delta_y;
        Oled_SetCursor(text_x, text_y);
        snprintf(str, sizeof(str), "Blue Value:    %d", local_value++);
        Oled_WriteString(str, &DEFAULT_FONT, White);

        if (local_value > 255) local_value = 0;
        Oled_UpdateScreen();
    }
    return NULL;
}

void* Task_NRF_Receiver(void *parameter)
{
    // TODO: bật lại khi có package nrf24l01 và rgb_led
    // RGBLed_Init();
    // NRF_RX_Mode_Init(addr, channel);
    // NRF_StartListening();
    uint8_t dummy_data = 0;
    while (1)
    {
        printf("NRF Receiver Task running... %d\n", dummy_data++);
        msleep(5000);
    }
    return NULL;
}

void On_MQTT_HandleMessage(const char* topic, const char* payload);

void* MQTT_Task(void *parameter)
{
    const uint32_t reconnect_delay_ms    = 2000;
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

    uint8_t led_value = 0;
    while (1)
    {
        pthread_mutex_lock(&rgb_data_mutex);
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

        {
            char payload[128];
            int  publish_rc;

            // Tách increment ra khỏi snprintf để tránh undefined behavior
            snprintf(payload, sizeof(payload),
                     "{\"Red\": %d, \"Green\": %d, \"Blue\": %d}",
                     led_value, led_value + 1, led_value + 2);
            led_value += 3;

            do {
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

            if (led_value > 252) led_value = 0;
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

    pthread_mutex_lock(&rgb_data_mutex);
    printf("Received MQTT message on topic '%s': Red=%d, Green=%d, Blue=%d\n",
           topic, red, green, blue);
    // TODO: bật lại khi có rgb_led
    // rgb_led.red_value   = red;
    // rgb_led.green_value = green;
    // rgb_led.blue_value  = blue;
    // last_rgb_led = rgb_led;
    pthread_mutex_unlock(&rgb_data_mutex);

    cJSON_Delete(root);
}
```

---

## 5. Các lỗi đã fix

| # | Lỗi gốc | Sửa thành | Lý do |
|---|---|---|---|
| 1 | `while (1) v` | `while (1)` | Ký tự `v` thừa, syntax error |
| 2 | `unit8_t led_value` | `uint8_t led_value` | Typo, type không tồn tại |
| 3 | Dòng `va` thừa | Xóa | Token lạ, syntax error |
| 4 | `snprintf(..., led_value++, led_value++, led_value++)` | Tách thành `led_value, led_value+1, led_value+2` rồi `led_value += 3` | Undefined behavior — thứ tự evaluation của argument trong C không xác định |
| 5 | `pthread_create(NULL, ...)` | `pthread_create(&t_oled, ...)` | NULL thread handle gây undefined behavior |
| 6 | `SCHED_FIFO` không có `CAP_SYS_NICE` | Comment lại, dùng `SCHED_OTHER` | `pthread_create` trả về `EPERM` → thread không tạo được → segfault |

---

## 6. Vấn đề SCHED_FIFO và Segmentation Fault

### Nguyên nhân segfault

```
pthread_create(..., SCHED_FIFO, priority=50)
    → trả về EPERM (thiếu CAP_SYS_NICE)
    → thread KHÔNG được tạo
    → main() tiếp tục chạy
    → các hàm trong thread cố gọi hardware chưa init
    → SEGMENTATION FAULT
```

### Kích hoạt lại SCHED_FIFO khi deploy

**Cách 1 — chạy với `chrt`** (khuyến nghị cho embedded):
```bash
chrt -f 1 /usr/bin/app
```

**Cách 2 — cấp capability cho binary**:
```bash
setcap cap_sys_nice+ep /usr/bin/app
```

**Cách 3 — init script tự động khi boot**, tạo file `package/app/S99app`:
```sh
#!/bin/sh
case "$1" in
    start) chrt -f 1 /usr/bin/app & ;;
    stop)  killall app ;;
esac
```

Thêm vào `app.mk`:
```makefile
define APP_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/app     $(TARGET_DIR)/usr/bin/app
    $(INSTALL) -D -m 0755 $(TOPDIR)/package/app/S99app \
                                         $(TARGET_DIR)/etc/init.d/S99app
endef
```

Sau khi bật lại SCHED_FIFO, bỏ comment phần TODO trong `main()` và set priority đúng cho từng thread:

```c
pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

// NRF: priority cao nhất — nhận data không được trễ
param.sched_priority = NRF_RECEIVER_TASK_PRIORITY; // 60
pthread_attr_setschedparam(&attr, &param);
pthread_create(&t_nrf, &attr, Task_NRF_Receiver, NULL);

// OLED: priority trung bình — hiển thị mượt
param.sched_priority = OLED_TASK_PRIORITY;         // 50
pthread_attr_setschedparam(&attr, &param);
pthread_create(&t_oled, &attr, Task_Oled, NULL);

// MQTT: priority thấp nhất — publish 1s/lần, chậm cũng được
param.sched_priority = MQTT_TASK_PRIORITY;         // 40
pthread_attr_setschedparam(&attr, &param);
pthread_create(&t_mqtt, &attr, MQTT_Task, NULL);
```

---

## 7. Build và deploy

```bash
# Build
make app

# Copy lên board
scp ~/Documents/buildroot/output/target/usr/bin/app root@192.168.7.2:/usr/bin/app

# Chạy thường
/usr/bin/app

# Chạy với realtime scheduling
chrt -f 1 /usr/bin/app
```

---

## 8. TODO

- [ ] Tạo package `nrf24l01` và bỏ comment trong `Task_NRF_Receiver`
- [ ] Tạo package `rgb_led` và bỏ comment trong `On_MQTT_HandleMessage`
- [ ] Bật lại `SCHED_FIFO` khi deploy production (xem mục 6)
- [ ] Kết nối `rgb_led` global variable giữa các task qua mutex
