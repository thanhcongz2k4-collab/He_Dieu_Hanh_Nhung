# MQTT trên BeagleBone Black với Buildroot

Hướng dẫn đầy đủ cấu hình, kết nối mạng, build thư viện MQTT và chạy ứng dụng test trên BeagleBone Black (BBB) sử dụng Buildroot.

---

## Mục lục

1. [Tổng quan hệ thống](#1-tổng-quan-hệ-thống)
2. [Cấu hình Buildroot – bật gói MQTT](#2-cấu-hình-buildroot--bật-gói-mqtt)
3. [Kết nối mạng Ubuntu ↔ BeagleBone (RNDIS)](#3-kết-nối-mạng-ubuntu--beaglebone-rndis)
4. [Cấu trúc thư mục package](#4-cấu-trúc-thư-mục-package)
5. [Source code đầy đủ – mqtt library](#5-source-code-đầy-đủ--mqtt-library)
6. [Source code đầy đủ – mqtt\_test](#6-source-code-đầy-đủ--mqtt_test)
7. [Tích hợp vào Buildroot](#7-tích-hợp-vào-buildroot)
8. [Build và deploy lên BBB](#8-build-và-deploy-lên-bbb)
9. [Kiểm tra kết quả](#9-kiểm-tra-kết-quả)
10. [Ghi chú & troubleshooting](#10-ghi-chú--troubleshooting)

---

## 1. Tổng quan hệ thống

```
┌─────────────────────────────────────────────────────────────┐
│                      Ubuntu Host                            │
│                                                             │
│   Buildroot  ──build──►  rootfs image                      │
│                                                             │
│   wlp0s20f3 (WiFi) ──NAT──► enx02ddbbccdd02 (RNDIS USB)   │
└─────────────────────────┬───────────────────────────────────┘
                          │ USB (RNDIS)
                          │ 192.168.7.1 ↔ 192.168.7.2
┌─────────────────────────▼───────────────────────────────────┐
│                   BeagleBone Black                          │
│                                                             │
│   usb0 (192.168.7.2)  ──►  mqtt_test  ──►  broker.hivemq  │
└─────────────────────────────────────────────────────────────┘
                                                │
                                                │ MQTT publish
                                                ▼
                                    ┌───────────────────┐
                                    │  broker.hivemq.com │
                                    │  :1883             │
                                    └────────┬──────────┘
                                             │
                              ┌──────────────▼──────────────┐
                              │  MQTT Explorer / mosquitto  │
                              │  subscribe topic/test       │
                              └─────────────────────────────┘
```

---

## 2. Cấu hình Buildroot – bật gói MQTT

### 2.1 Bật các package cần thiết

```bash
cd ~/Documents/buildroot
make menuconfig
```

Bật các mục sau:

```
Target packages
  └── Networking applications
        └── [*] mosquitto
  └── Libraries
        └── Networking
              └── [*] paho-mqtt-c
              └── [*] paho-mqtt-cpp
```

### 2.2 Sửa mosquitto.mk – tắt ADNS

Mở file `package/mosquitto/mosquitto.mk`, đảm bảo có đoạn:

```makefile
ifeq ($(BR2_TOOLCHAIN_USES_GLIBC),y)
MOSQUITTO_MAKE_OPTS += WITH_ADNS=no
else
MOSQUITTO_MAKE_OPTS += WITH_ADNS=no
endif
```

### 2.3 Tạo DNS overlay

```bash
mkdir -p board/beagleboard/beaglebone/rootfs_overlay/etc
nano board/beagleboard/beaglebone/rootfs_overlay/etc/resolv.conf
```

Nội dung `resolv.conf`:

```
nameserver 8.8.8.8
nameserver 1.1.1.1
```

---

## 3. Kết nối mạng Ubuntu ↔ BeagleBone (RNDIS)

### 3.1 Xác định interface

```bash
ip link show
```

| Interface          | Vai trò                     |
|--------------------|-----------------------------|
| `wlp0s20f3`        | WiFi – có Internet          |
| `enx02ddbbccdd02`  | RNDIS USB – kết nối BBB     |

### 3.2 Cấu hình trên Ubuntu

```bash
# Đặt IP cho interface RNDIS
sudo ip addr add 192.168.7.1/24 dev enx02ddbbccdd02
sudo ip link set enx02ddbbccdd02 up

# Bật IP forwarding
sudo sysctl -w net.ipv4.ip_forward=1

# NAT – chia sẻ Internet từ WiFi sang BBB
sudo iptables -t nat -A POSTROUTING -o wlp0s20f3 -j MASQUERADE
sudo iptables -A FORWARD -i enx02ddbbccdd02 -o wlp0s20f3 -j ACCEPT
sudo iptables -A FORWARD -i wlp0s20f3 -o enx02ddbbccdd02 -m state \
     --state RELATED,ESTABLISHED -j ACCEPT
```

### 3.3 Cấu hình trên BeagleBone

```bash
ip addr add 192.168.7.2/24 dev usb0
ip link set usb0 up
ip route add default via 192.168.7.1
```

### 3.4 Kiểm tra kết nối

```bash
# Trên BBB
ping 8.8.8.8          # test IP
ping google.com       # test DNS
```

---

## 4. Cấu trúc thư mục package

```
buildroot/
└── package/
    ├── mqtt/                      # Thư viện wrapper paho-mqtt-c
    │   ├── Config.in
    │   ├── mqtt.mk
    │   └── src/
    │       ├── mqtt.h
    │       └── mqtt.c
    │
    └── mqtt_test/                 # App test – publish định kỳ
        ├── Config.in
        ├── mqtt_test.mk
        └── src/
            └── main.c
```

---

## 5. Source code đầy đủ – mqtt library

### 5.1 `package/mqtt/src/mqtt.h`

```c
#ifndef __MQTT__
#define __MQTT__

#if !defined(TEST_STM32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define MQTT_ADDRESS     "tcp://broker.hivemq.com:1883"
#define MQTT_CLIENTID    "BBB_Client_Rimuru_001"
#define MQTT_QOS         1
#define MQTT_TIMEOUT     10000L
#define MQTT_RETAINED    1

typedef void (*MQTT_HandleMessage)(const char* topic, const char* payload);

int  MQTT_Init(MQTT_HandleMessage handler);
int  MQTT_Reconnect(void);
int  MQTT_IsConnected(void);
int  MQTT_SubscribeTopic(const char* topic);
int  MQTT_PublishMessage(const char* topic, const char* payload);
void MQTT_Disconnect(void);

#endif /* TEST_STM32 */
#endif /* __MQTT__ */
```

### 5.2 `package/mqtt/src/mqtt.c`

```c
#include "mqtt.h"

static volatile MQTTClient_deliveryToken deliveredtoken;
static MQTTClient client = NULL;
static MQTT_HandleMessage message_handler = NULL;
static volatile int mqtt_connected = 0;
static int mqtt_client_created = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int MQTT_ConnectInternal(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession      = 1;

    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS)
    {
        mqtt_connected = 0;
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }

    mqtt_connected = 1;
    printf("MQTT connected successfully\n");
    return MQTTCLIENT_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                            */
/* ------------------------------------------------------------------ */

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    printf("Message with token value %d delivered\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen,
             MQTTClient_message *message)
{
    char payload[256] = {0};
    char topic[128]   = {0};

    int len = message->payloadlen;
    if (len >= (int)sizeof(payload))
        len = (int)sizeof(payload) - 1;
    memcpy(payload, message->payload, len);
    payload[len] = '\0';

    int tlen = (topicLen > 0) ? topicLen : (int)strlen(topicName);
    if (tlen >= (int)sizeof(topic))
        tlen = (int)sizeof(topic) - 1;
    memcpy(topic, topicName, tlen);
    topic[tlen] = '\0';

    printf("Message arrived\nTopic: %s\nMessage: %s\n", topic, payload);

    if (message_handler)
        message_handler(topic, payload);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
    mqtt_connected = 0;
    printf("Connection lost: %s\n", (cause != NULL) ? cause : "unknown");
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int MQTT_IsConnected(void)
{
    if (!mqtt_client_created) return 0;
    if (!mqtt_connected)      return 0;

    if (MQTTClient_isConnected(client) == 0)
    {
        mqtt_connected = 0;
        return 0;
    }
    return 1;
}

int MQTT_Init(MQTT_HandleMessage handler)
{
    int rc;
    message_handler = handler;

    if (!mqtt_client_created)
    {
        rc = MQTTClient_create(&client, MQTT_ADDRESS, MQTT_CLIENTID,
                               MQTTCLIENT_PERSISTENCE_NONE, NULL);
        if (rc != MQTTCLIENT_SUCCESS)
        {
            printf("Failed to create MQTT client, return code %d\n", rc);
            return rc;
        }

        rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
        if (rc != MQTTCLIENT_SUCCESS)
        {
            printf("Failed to set MQTT callbacks, return code %d\n", rc);
            MQTTClient_destroy(&client);
            return rc;
        }
        mqtt_client_created = 1;
    }

    if (MQTT_IsConnected())
        return MQTTCLIENT_SUCCESS;

    return MQTT_ConnectInternal();
}

int MQTT_Reconnect(void)
{
    if (!mqtt_client_created)
    {
        if (message_handler == NULL)
            return MQTTCLIENT_FAILURE;
        return MQTT_Init(message_handler);
    }

    if (MQTT_IsConnected())
        return MQTTCLIENT_SUCCESS;

    return MQTT_ConnectInternal();
}

int MQTT_SubscribeTopic(const char *topic)
{
    if (!MQTT_IsConnected())
        return MQTTCLIENT_DISCONNECTED;

    printf("Subscribing to topic %s for client %s using QoS %d\n",
           topic, MQTT_CLIENTID, MQTT_QOS);

    int rc = MQTTClient_subscribe(client, topic, MQTT_QOS);
    if (rc != MQTTCLIENT_SUCCESS)
        printf("Failed to subscribe, return code %d\n", rc);

    return rc;
}

int MQTT_PublishMessage(const char *topic, const char *payload)
{
    int rc;

    if (!MQTT_IsConnected())
        return MQTTCLIENT_DISCONNECTED;

    MQTTClient_message       pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    pubmsg.payload    = (char *)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos        = MQTT_QOS;
    pubmsg.retained   = MQTT_RETAINED;

    printf("Publishing message: %s\n", payload);

    rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS)
    {
        if (rc == MQTTCLIENT_DISCONNECTED)
            mqtt_connected = 0;
        printf("Failed to publish, return code %d\n", rc);
        return rc;
    }

    MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
    printf("Message with delivery token %d delivered\n", token);
    return rc;
}

void MQTT_Disconnect(void)
{
    if (!mqtt_client_created) return;

    if (MQTT_IsConnected())
        MQTTClient_disconnect(client, 10000);

    MQTTClient_destroy(&client);
    mqtt_connected      = 0;
    mqtt_client_created = 0;
}
```

### 5.3 `package/mqtt/mqtt.mk`

```makefile
################################################################################
#
# mqtt  –  thin wrapper library around paho-mqtt-c
#
################################################################################

MQTT_VERSION     = 1.0
MQTT_SITE        = $(TOPDIR)/package/mqtt/src
MQTT_SITE_METHOD = local

MQTT_DEPENDENCIES    = paho-mqtt-c
MQTT_INSTALL_STAGING = YES

define MQTT_BUILD_CMDS
	# shared library: libmqtt.so
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC -shared \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/paho-mqtt3c \
		$(@D)/mqtt.c \
		-o $(@D)/libmqtt.so \
		-L$(STAGING_DIR)/usr/lib -lpaho-mqtt3c

	# object file
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/paho-mqtt3c \
		-c $(@D)/mqtt.c \
		-o $(@D)/mqtt.o

	# static library: libmqtt.a
	$(TARGET_AR) rcs $(@D)/libmqtt.a $(@D)/mqtt.o
endef

define MQTT_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mqtt.h      $(STAGING_DIR)/usr/include/mqtt.h
	$(INSTALL) -D -m 0755 $(@D)/libmqtt.so  $(STAGING_DIR)/usr/lib/libmqtt.so
	$(INSTALL) -D -m 0644 $(@D)/libmqtt.a   $(STAGING_DIR)/usr/lib/libmqtt.a
endef

define MQTT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libmqtt.so  $(TARGET_DIR)/usr/lib/libmqtt.so
endef

$(eval $(generic-package))
```

### 5.4 `package/mqtt/Config.in`

```
config BR2_PACKAGE_MQTT
	bool "mqtt"
	depends on BR2_PACKAGE_PAHO_MQTT_C
	help
	  Thin wrapper library around paho-mqtt-c.
	  Installs libmqtt.so, libmqtt.a, mqtt.h.
	  Depends on: paho-mqtt-c
```

---

## 6. Source code đầy đủ – mqtt_test

### 6.1 `package/mqtt_test/src/main.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mqtt.h"

#define PUBLISH_TOPIC    "topic/test"
#define PUBLISH_INTERVAL  2             /* seconds */

static void on_message(const char *topic, const char *payload)
{
    printf("[SUB] topic: %s | message: %s\n", topic, payload);
}

int main(void)
{
    int rc;
    int cnt = 0;
    char payload[64];

    printf("=== MQTT Test – BeagleBone ===\n");
    printf("Broker  : %s\n", MQTT_ADDRESS);
    printf("Topic   : %s\n", PUBLISH_TOPIC);
    printf("Interval: %d s\n\n", PUBLISH_INTERVAL);

    rc = MQTT_Init(on_message);
    if (rc != 0)
    {
        fprintf(stderr, "MQTT_Init failed (rc=%d)\n", rc);
        return EXIT_FAILURE;
    }

    while (1)
    {
        if (!MQTT_IsConnected())
        {
            printf("[WARN] Disconnected – reconnecting...\n");
            rc = MQTT_Reconnect();
            if (rc != 0)
            {
                printf("[WARN] Reconnect failed (rc=%d), retry in %ds\n",
                       rc, PUBLISH_INTERVAL);
                sleep(PUBLISH_INTERVAL);
                continue;
            }
        }

        snprintf(payload, sizeof(payload), "Hello Rimuru : %d", cnt++);
        rc = MQTT_PublishMessage(PUBLISH_TOPIC, payload);
        if (rc != 0)
            fprintf(stderr, "[ERR] Publish failed (rc=%d)\n", rc);

        sleep(PUBLISH_INTERVAL);
    }

    MQTT_Disconnect();
    return EXIT_SUCCESS;
}
```

### 6.2 `package/mqtt_test/mqtt_test.mk`

```makefile
################################################################################
#
# mqtt_test  –  periodic MQTT publisher for BeagleBone
#               sends "Hello Rimuru : <cnt>" to topic/test every 2 s
#
################################################################################

MQTT_TEST_VERSION     = 1.0
MQTT_TEST_SITE        = $(TOPDIR)/package/mqtt_test/src
MQTT_TEST_SITE_METHOD = local

MQTT_TEST_DEPENDENCIES = mqtt paho-mqtt-c

define MQTT_TEST_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(STAGING_DIR)/usr/include \
		-I$(TOPDIR)/package/mqtt/src \
		$(@D)/main.c \
		-o $(@D)/mqtt_test \
		-L$(STAGING_DIR)/usr/lib -lmqtt -lpaho-mqtt3c
endef

define MQTT_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mqtt_test $(TARGET_DIR)/usr/bin/mqtt_test
endef

$(eval $(generic-package))
```

### 6.3 `package/mqtt_test/Config.in`

```
config BR2_PACKAGE_MQTT_TEST
	bool "mqtt_test"
	depends on BR2_PACKAGE_MQTT
	help
	  Periodic MQTT publisher running on BeagleBone.
	  Sends "Hello Rimuru : <cnt>" to topic/test every 2 seconds.
	  Depends on the mqtt wrapper library.
```

---

## 7. Tích hợp vào Buildroot

Thêm vào `package/Config.in`:

```
menu "My Libraries"
  source "package/mqtt/Config.in"
  source "package/mqtt_test/Config.in"
endmenu
```

Bật trong menuconfig:

```bash
make menuconfig
# Target packages → My Libraries → [*] mqtt
#                                → [*] mqtt_test
```

---

## 8. Build và deploy lên BBB

### 8.1 Build

```bash
cd ~/Documents/buildroot

# Build theo thứ tự
make mqtt-rebuild
make mqtt_test-rebuild
```

### 8.2 Copy lên BBB qua SSH

```bash
# Binary mqtt_test
scp output/build/mqtt_test-1.0/mqtt_test root@192.168.7.2:/usr/bin/

# Shared library
scp output/build/mqtt-1.0/libmqtt.so root@192.168.7.2:/usr/lib/

# paho library (nếu chưa có trong rootfs)
scp output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libpaho-mqtt3c.so* \
    root@192.168.7.2:/usr/lib/
```

### 8.3 Cấp quyền thực thi

```bash
# Trên BBB
chmod +x /usr/bin/mqtt_test
chmod +x /usr/lib/libmqtt.so
```

---

## 9. Kiểm tra kết quả

### Chạy trên BBB

```bash
mqtt_test
```

Output mong đợi:

```
=== MQTT Test – BeagleBone ===
Broker  : tcp://broker.hivemq.com:1883
Topic   : topic/test
Interval: 2 s

MQTT connected successfully
Publishing message: Hello Rimuru : 0
Message with delivery token 1 delivered
Publishing message: Hello Rimuru : 1
Message with delivery token 1 delivered
...
```

### Subscribe trên Ubuntu

```bash
mosquitto_sub -h broker.hivemq.com -t "topic/test"
```

### Subscribe bằng MQTT Explorer

| Field     | Giá trị                  |
|-----------|--------------------------|
| Host      | `broker.hivemq.com`      |
| Port      | `1883`                   |
| Client ID | `Explorer_Rimuru_001`    |
| Topic     | `topic/test`             |

---

## 10. Ghi chú & troubleshooting

| Triệu chứng | Nguyên nhân | Giải pháp |
|---|---|---|
| `cannot find -lmqtt` | mqtt chưa install staging | Chạy `make mqtt-rebuild` trước |
| `Connection lost` liên tục | Trùng `MQTT_CLIENTID` trên broker public | Đổi `MQTT_CLIENTID` thành unique |
| `ping google.com` fail | DNS chưa cấu hình | Kiểm tra `resolv.conf` trong overlay |
| MQTT Explorer không nhận | Subscribe sai topic | Đổi topic thành `topic/test` |
| `mqtt.h: No such file` | Thiếu `-I$(TOPDIR)/package/mqtt/src` | Thêm include path vào `mqtt_test.mk` |
| BBB không nhận RNDIS | gadget script lỗi | Kiểm tra `os_desc` trong USB gadget script |
