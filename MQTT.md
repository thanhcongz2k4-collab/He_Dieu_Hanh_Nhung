
# Hướng dẫn USB RNDIS + MQTT cho BeagleBone (Buildroot)

> Kernel tham chiếu: 6.16.5

Tài liệu này gồm 2 phần:

1. Cấu hình Ethernet qua USB (RNDIS) để giao tiếp với host.
2. Cấu hình và test MQTT client trên BeagleBone.

## Mục tiêu

Sau khi hoàn tất:

- BeagleBone tạo interface `usb0` với IP `192.168.7.2/24`.
- Host (Windows/WSL) ping được `192.168.7.2` qua cáp USB.
- Board có thể kết nối broker MQTT ngoài và publish/subscribe.

---

## Phần A - Ethernet qua USB (RNDIS)

### A1. Bật cấu hình kernel cho USB RNDIS

```bash
cd ~/beaglebone/buildroot
make linux-menuconfig
```

Bật các mục sau:

```text
Device Drivers  --->
    [*] USB support  --->
        <*>   Support for Host-side USB
        <*>   USB Gadget Support  --->
                <M>   USB Gadget functions configurable through configfs
                [*]     RNDIS
        <*>   Inventra Highspeed Dual Role Controller
                MUSB Mode Selection (Dual Role mode)  --->
                <*>     OMAP2430 and onwards
                <*>     TI DSPS platforms
        USB Physical Layer drivers  --->
                <*> NOP USB Transceiver Driver
                <*> AM335x USB PHY Driver
```

### A2. Tạo script khởi động USB gadget

Tạo file:

`board/beagleboard/beaglebone/rootfs_overlay/etc/init.d/S45usbgadget`

```bash
sudo mkdir -p board/beagleboard/beaglebone/rootfs_overlay/etc/init.d
sudo nano board/beagleboard/beaglebone/rootfs_overlay/etc/init.d/S45usbgadget
```

Nội dung script:

```sh
#!/bin/sh
#
# USB RNDIS Gadget setup via configfs
#

GADGET_DIR=/sys/kernel/config/usb_gadget
GADGET_NAME=g0
GADGET=$GADGET_DIR/$GADGET_NAME

# USB IDs - dung ID cua Linux Foundation RNDIS
VENDOR_ID="0x0525"
PRODUCT_ID="0xa4a2"
MANUFACTURER="BeagleBone"
PRODUCT="BeagleBone RNDIS"
SERIAL="deadbeef12345678"

# MAC cho device (usb0) va host
# Quan trong: byte dau cua DEV_ADDR nen la 0x02 (locally administered)
DEV_ADDR="02:dd:bb:cc:dd:01"
HOST_ADDR="02:dd:bb:cc:dd:02"

start() {
    echo "Starting USB RNDIS gadget..."

    # Load module can thiet
    modprobe libcomposite 2>/dev/null
    modprobe usb_f_rndis 2>/dev/null

    # Mount configfs neu chua mount
    if ! grep -q configfs /proc/mounts; then
        mount -t configfs none /sys/kernel/config
    fi

    # Kiem tra UDC
    UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    if [ -z "$UDC" ]; then
        echo "ERROR: No UDC found!"
        return 1
    fi
    echo "Found UDC: $UDC"

    # Don dep gadget cu neu co
    if [ -d "$GADGET" ]; then
        echo "" > "$GADGET/UDC" 2>/dev/null
        sleep 0.5
        rm -rf "$GADGET/configs/c.1/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null
        rmdir "$GADGET/configs/c.1" 2>/dev/null
        rmdir "$GADGET/functions/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/strings/0x409" 2>/dev/null
        rmdir "$GADGET" 2>/dev/null
    fi

    # Tao gadget
    mkdir -p "$GADGET"

    # Set USB IDs
    echo "$VENDOR_ID" > "$GADGET/idVendor"
    echo "$PRODUCT_ID" > "$GADGET/idProduct"
    echo "0x0200" > "$GADGET/bcdUSB"      # USB 2.0
    echo "0x0100" > "$GADGET/bcdDevice"

    # Set strings
    mkdir -p "$GADGET/strings/0x409"
    echo "$MANUFACTURER" > "$GADGET/strings/0x409/manufacturer"
    echo "$PRODUCT" > "$GADGET/strings/0x409/product"
    echo "$SERIAL" > "$GADGET/strings/0x409/serialnumber"

    # Tao RNDIS function
    mkdir -p "$GADGET/functions/rndis.usb0"
    echo "$DEV_ADDR" > "$GADGET/functions/rndis.usb0/dev_addr"
    echo "$HOST_ADDR" > "$GADGET/functions/rndis.usb0/host_addr"

    # Quan trong: OS descriptor de Windows nhan dung RNDIS
    echo "1" > "$GADGET/os_desc/use"
    echo "0xcd" > "$GADGET/os_desc/b_vendor_code"
    echo "MSFT100" > "$GADGET/os_desc/qw_sign"

    echo "RNDIS" > "$GADGET/functions/rndis.usb0/os_desc/interface.rndis/compatible_id"
    echo "5162001" > "$GADGET/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id"

    # Tao configuration
    mkdir -p "$GADGET/configs/c.1"
    echo 500 > "$GADGET/configs/c.1/MaxPower"

    mkdir -p "$GADGET/configs/c.1/strings/0x409"
    echo "RNDIS Config" > "$GADGET/configs/c.1/strings/0x409/configuration"

    # Link OS descriptor voi config
    ln -s "$GADGET/configs/c.1" "$GADGET/os_desc/c.1"

    # Link function vao config
    ln -s "$GADGET/functions/rndis.usb0" "$GADGET/configs/c.1/rndis.usb0"

    # Bind gadget vao UDC
    echo "$UDC" > "$GADGET/UDC"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to bind gadget to UDC $UDC"
        return 1
    fi

    echo "USB RNDIS gadget started, UDC: $UDC"

    # Cau hinh interface usb0
    sleep 1
    if ip link show usb0 > /dev/null 2>&1; then
        ip addr add 192.168.7.2/24 dev usb0
        ip link set usb0 up
        echo "usb0 configured: 192.168.7.2/24"

        ip route add default via 192.168.7.1 dev usb0
        echo "Default route via 192.168.7.1 set"
    else
        echo "WARNING: usb0 interface not found yet"
    fi
}

stop() {
    echo "Stopping USB RNDIS gadget..."

    ip link set usb0 down 2>/dev/null
    ip addr flush dev usb0 2>/dev/null

    if [ -d "$GADGET" ]; then
        echo "" > "$GADGET/UDC" 2>/dev/null
        sleep 0.5
        rm -f "$GADGET/os_desc/c.1" 2>/dev/null
        rm -f "$GADGET/configs/c.1/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null
        rmdir "$GADGET/configs/c.1" 2>/dev/null
        rmdir "$GADGET/functions/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/strings/0x409" 2>/dev/null
        rmdir "$GADGET/os_desc" 2>/dev/null
        rmdir "$GADGET" 2>/dev/null
    fi
    echo "USB RNDIS gadget stopped"
}

case "$1" in
    start) start ;;
    stop) stop ;;
    restart) stop; start ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac

exit 0
```

Cấp quyền thực thi cho script:

```bash
sudo chmod +x board/beagleboard/beaglebone/rootfs_overlay/etc/init.d/S45usbgadget
```

### A3. Thêm rootfs overlay vào Buildroot

```bash
cd ~/beaglebone/buildroot
make menuconfig
```

Vào:

- `System configuration` -> `Root filesystem overlay directories`

Điền giá trị:

```text
board/beagleboard/beaglebone/rootfs_overlay
```

Tùy chọn (khuyến nghị): đặt mật khẩu root để SSH sau này.

```text
System configuration  --->
    Root password: <your_password_here>
```

Nếu cần SSH, bật 1 trong 2 server (chỉ nên bật 1 cái):

```text
Target packages  --->
    Networking applications  --->
    [*] dropbear   (khuyến nghị cho Buildroot, nhẹ)
    [ ] openssh
```

### A4. Build và ghi image

```bash
cd ~/beaglebone/buildroot
make
```

Ghi image ra thẻ (đổi `/dev/sde` thành thiết bị đúng):

```bash
sudo dd if=output/images/sdcard.img of=/dev/sde bs=4M status=progress conv=fsync
sync
```

### A5. Kiểm tra sau khi boot

Trên BeagleBone:

```bash
ip a show usb0
```

Kỳ vọng có IP `192.168.7.2/24` trên `usb0`.

Trên Windows/WSL:

1. Vào `Control Panel` -> `Network and Internet` -> `Network Connections`.
2. Tìm adapter RNDIS (thường là `Ethernet` hoặc `Remote NDIS based Internet Sharing Device`).
3. Đặt IPv4 tĩnh:
   - IP: `192.168.7.1`
   - Subnet mask: `255.255.255.0`
    - Default gateway: để trống

Thử ping:

```bash
ping 192.168.7.2
```

---

## Phần B - Cấu hình MQTT client trên BeagleBone

### B1. Bật các gói cần thiết

```bash
cd ~/beaglebone/buildroot
make menuconfig
```

Bật các mục:

- `Target packages` -> `Networking applications` -> `mosquitto`
- `Target packages` -> `Libraries` -> `Networking` -> `paho-mqtt-c`
- `Target packages` -> `Libraries` -> `Networking` -> `paho-mqtt-cpp`

### B2. Sửa `mosquitto.mk` để tránh lỗi ADNS

Trong `package/mosquitto/mosquitto.mk`, đảm bảo có dòng sau:

```makefile
# adns uses getaddrinfo_a
ifeq ($(BR2_TOOLCHAIN_USES_GLIBC),y)
MOSQUITTO_MAKE_OPTS += WITH_ADNS=no
else
MOSQUITTO_MAKE_OPTS += WITH_ADNS=no
endif
```

Mục đích: tránh lỗi liên quan `adns` trong quá trình build.

### B3. Tạo DNS overlay

```bash
mkdir -p board/beagleboard/beaglebone/rootfs_overlay/etc
nano board/beagleboard/beaglebone/rootfs_overlay/etc/resolv.conf
```

Nội dung `resolv.conf`:

```text
nameserver 8.8.8.8
nameserver 1.1.1.1
```

### B4. Chia sẻ Internet từ Windows qua RNDIS

Trên Windows:

1. `Control Panel` -> `Network and Internet` -> `Network Connections`.
2. Chuột phải adapter đang có Internet (ví dụ Wi-Fi) -> `Properties` -> `Sharing`.
3. Bật `Allow other network users to connect through this computer's Internet connection`.
4. Chọn adapter RNDIS trong danh sách.

### B5. Test kết nối mạng và MQTT

Trên BeagleBone:

```bash
ping 8.8.8.8
ping google.com
mosquitto_pub -h test.mosquitto.org -t "test/topic" -m "Hello from BeagleBone!"
```

Trên host (để subscribe test):

```bash
mosquitto_sub -h test.mosquitto.org -t "test/topic"
```

---

## Phần C - Tạo app MQTT mẫu (`mqtt_test`) trong Buildroot

### C1. Tạo file `mqtt_test.c`

Đường dẫn: `package/mqtt_test/mqtt_test.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "BBB_Test_Client"
#define TOPIC       "test/bbb"
#define QOS         1
#define TIMEOUT     10000L

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt) {
    printf("Message with token value %d delivered\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    printf("Message arrived\nTopic: %s\nMessage: %.*s\n", topicName, message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Subscribing to topic %s\nfor client %s using QoS %d\n\n", TOPIC, CLIENTID, QOS);
    MQTTClient_subscribe(client, TOPIC, QOS);

    for (int i = 1; i <= 5; i++) {
        char payload[50];
        sprintf(payload, "Hello HiveMQ %d", i);
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = payload;
        pubmsg.payloadlen = (int)strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;

        MQTTClient_deliveryToken token;
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        printf("Publishing message: %s\n", payload);
        MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("Message with delivery token %d delivered\n", token);
        sleep(1);
    }

    printf("Waiting for messages. Press Enter to exit.\n");
    getchar();

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

### C2. Tạo file `mqtt_test.mk`

Đường dẫn: `package/mqtt_test/mqtt_test.mk`

```makefile
MQTT_TEST_VERSION     = 1.0
MQTT_TEST_SITE        = $(TOPDIR)/package/mqtt_test
MQTT_TEST_SITE_METHOD = local

MQTT_TEST_DEPENDENCIES = paho-mqtt-c

define MQTT_TEST_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/paho-mqtt3c \
		$(@D)/mqtt_test.c \
		-o $(@D)/mqtt_test \
		-L$(STAGING_DIR)/usr/lib -lpaho-mqtt3c
endef

define MQTT_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mqtt_test $(TARGET_DIR)/usr/bin/mqtt_test
endef

$(eval $(generic-package))
```

### C3. Tạo file `Config.in` cho gói mới

Đường dẫn: `package/mqtt_test/Config.in`

```makefile
config BR2_PACKAGE_MQTT_TEST
    bool "mqtt_test"
    help
      A small MQTT test program using Paho C library.
      Publishes and subscribes to HiveMQ.
      Depends on paho-mqtt-c.
```

### C4. Thêm vào `package/Config.in`

```makefile
menu "My Libraries"
  source "package/mqtt_test/Config.in"
endmenu
```

### C5. Build lại và chạy thử

```bash
cd ~/beaglebone/buildroot
make
```

Nạp image vào SD/eMMC, boot board, sau đó chạy:

```bash
mqtt_test
```

---

## Ghi chú nhanh

- Nếu host không nhận RNDIS, kiểm tra lại `os_desc` trong script gadget.
- Nếu board không resolve được domain, kiểm tra `resolv.conf` trong overlay.
- Khi test MQTT công khai, ưu tiên topic riêng để tránh trùng với người khác.
