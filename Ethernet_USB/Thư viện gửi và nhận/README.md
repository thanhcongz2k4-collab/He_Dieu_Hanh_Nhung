# Hệ Thống thư viện truyền nhận qua UDP

Truyền dữ liệu JSON từ **BeagleBone Black (BBB)** lên **PC Ubuntu** và nhận lệnh điều khiển LED ngược lại, thông qua giao thức **UDP** trên kết nối USB Ethernet (`usb0`).

---

## 1. Kiến trúc Hệ thống

```
[BeagleBone Black]                        [PC Ubuntu]
  APP_ADD_JSON (C)   ── UDP 8080 ──>   control_app.py (Python/Tkinter)
  (gửi JSON sensor)  <── UDP 8080 ──   (gửi lệnh LED_ON / LED_OFF)
        |
  libbbb_net.a  (thư viện tĩnh UDP)
  libcjson.a    (thư viện JSON)
```

- **Interface mạng:** `usb0` (USB Gadget Ethernet)
- **IP Ubuntu:** `192.168.7.1`
- **IP BBB:** `192.168.7.2`
- **Port:** `8080` (UDP)

---

## 2. Cấu trúc Thư mục (Tree)

```
buildroot/
└── package/
    ├── libbbb_net/                  # Thư viện tĩnh xử lý UDP
    │   ├── Config.in
    │   ├── libbbb_net.mk
    │   └── src/
    │       ├── Makefile
    │       ├── bbb_net.h
    │       └── bbb_net.c
    │
    └── APP_ADD_JSON/                # Ứng dụng chính chạy trên BBB
        ├── Config.in
        ├── APP_ADD_JSON.mk
        └── src/
            ├── Makefile
            └── main.c

(Thư mục PC — chạy thủ công trên Ubuntu)
control_app.py                       # Giao diện Python điều khiển từ PC
```

---

## 3. Thư viện tĩnh `libbbb_net`

### `package/libbbb_net/src/bbb_net.h`

```c
#ifndef BBB_NET_H
#define BBB_NET_H

#include <arpa/inet.h>

// Khởi tạo socket UDP, bind để nhận và cấu hình địa chỉ đích
// Trả về file descriptor hoặc -1 nếu lỗi
int bbb_net_init(const char *ip, int port);

// Gửi chuỗi dữ liệu qua UDP
int bbb_net_send(int fd, const char *data);

// Nhận dữ liệu (blocking — chờ cho đến khi có gói tin)
int bbb_net_receive(int fd, char *buffer, int size);

// Đóng socket
void bbb_net_close(int fd);

#endif
```

### `package/libbbb_net/src/bbb_net.c`

```c
#include "bbb_net.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct sockaddr_in dest_addr;

int bbb_net_init(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(ip);

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind failed");
        return -1;
    }

    return fd;
}

int bbb_net_send(int fd, const char *data) {
    return sendto(fd, data, strlen(data), 0,
                  (struct sockaddr *)&dest_addr, sizeof(dest_addr));
}

int bbb_net_receive(int fd, char *buffer, int size) {
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    int n = recvfrom(fd, buffer, size - 1, 0,
                     (struct sockaddr *)&src_addr, &addr_len);
    if (n > 0) buffer[n] = '\0';
    return n;
}

void bbb_net_close(int fd) {
    close(fd);
}
```

### `package/libbbb_net/src/Makefile`

```makefile
LIB_NAME = libbbb_net.a
OBJS     = bbb_net.o

all: $(LIB_NAME)

$(LIB_NAME): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(LIB_NAME)
```

### `package/libbbb_net/libbbb_net.mk`

```makefile
LIBBBB_NET_VERSION      = 1.0
LIBBBB_NET_SITE         = $(TOPDIR)/package/libbbb_net/src
LIBBBB_NET_SITE_METHOD  = local
LIBBBB_NET_INSTALL_STAGING = YES

define LIBBBB_NET_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define LIBBBB_NET_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/bbb_net.h    $(STAGING_DIR)/usr/include/bbb_net.h
	$(INSTALL) -D -m 0644 $(@D)/libbbb_net.a $(STAGING_DIR)/usr/lib/libbbb_net.a
endef

define LIBBBB_NET_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/bbb_net.h $(TARGET_DIR)/usr/include/bbb_net.h
endef

$(eval $(generic-package))
```

### `package/libbbb_net/Config.in`

```
config BR2_PACKAGE_LIBBBB_NET
	bool "libbbb_net"
	help
	  Thu vien tinh UDP cho BeagleBone Black (Nhom 2).
```

---

## 4. Ứng dụng chính `APP_ADD_JSON`

### `package/APP_ADD_JSON/src/main.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cjson/cJSON.h>
#include "bbb_net.h"

#define PC_IP  "192.168.7.1"
#define PORT    8080

int main() {
    char rx_buf[1024];

    int net_fd = bbb_net_init(PC_IP, PORT);
    if (net_fd < 0) {
        printf("Loi: Khong the mo ket noi UDP!\n");
        return -1;
    }

    printf("App Nhom 2 khoi chay thanh cong tren BBB...\n");

    while (1) {
        /* --- GỬI JSON LÊN PC --- */
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "device", "BBB_Nhom2");
        cJSON_AddNumberToObject(root, "temp",   36.5);   /* thay bằng giá trị sensor thật */

        char *json_out = cJSON_PrintUnformatted(root);
        bbb_net_send(net_fd, json_out);
        printf("Sent: %s\n", json_out);

        free(json_out);
        cJSON_Delete(root);

        /* --- NHẬN LỆNH TỪ PC (blocking) --- */
        printf("Dang cho lenh tu PC...\n");
        if (bbb_net_receive(net_fd, rx_buf, sizeof(rx_buf)) > 0) {
            printf("=> NHAN: %s\n", rx_buf);

            if (strstr(rx_buf, "LED_ON")) {
                printf("[ACTION] Bat LED\n");
                /* TODO: ghi vào /sys/class/gpio/gpioXXX/value */
            } else if (strstr(rx_buf, "LED_OFF")) {
                printf("[ACTION] Tat LED\n");
            }
        }

        sleep(1);
    }

    bbb_net_close(net_fd);
    return 0;
}
```

### `package/APP_ADD_JSON/src/Makefile`

```makefile
APP  = APP_ADD_JSON
SRCS = main.c
OBJS = $(SRCS:.c=.o)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(APP) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(APP) *.o
```

### `package/APP_ADD_JSON/APP_ADD_JSON.mk`

```makefile
APP_ADD_JSON_VERSION     = 1.0
APP_ADD_JSON_SITE        = $(TOPDIR)/package/APP_ADD_JSON/src
APP_ADD_JSON_SITE_METHOD = local
APP_ADD_JSON_DEPENDENCIES = cjson libbbb_net

define APP_ADD_JSON_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) \
		LDFLAGS="$(TARGET_LDFLAGS) -lbbb_net -lcjson -lm" \
		-C $(@D)
endef

define APP_ADD_JSON_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/APP_ADD_JSON $(TARGET_DIR)/usr/bin/APP_ADD_JSON
endef

$(eval $(generic-package))
```

### `package/APP_ADD_JSON/Config.in`

```
config BR2_PACKAGE_APP_ADD_JSON
	bool "app_add_json"
	select BR2_PACKAGE_CJSON
	select BR2_PACKAGE_LIBBBB_NET
	help
	  App doc sensor, dong goi JSON va day len PC qua UDP (usb0).
	  Project Nhom 2.
```

---

## 5. Giao diện Điều khiển trên PC (`control_app.py`)

Chạy trực tiếp trên **máy Ubuntu**, không cần biên dịch.

```python
import socket
import tkinter as tk
from threading import Thread

PC_IP  = "0.0.0.0"       # Lắng nghe trên tất cả interface
BBB_IP = "192.168.7.2"   # IP của BeagleBone Black
PORT   = 8080

class BBB_Controller:
    def __init__(self, window):
        self.window = window
        self.window.title("Nhom 2 - BBB Control Panel")
        self.window.geometry("420x320")
        self.window.resizable(False, False)

        tk.Label(window, text="Du lieu JSON tu BBB:", font=("Consolas", 10, "bold")).pack(pady=6)

        self.display = tk.Text(window, height=6, width=48, bg="#1e1e1e", fg="#00ff88",
                               font=("Consolas", 9))
        self.display.pack(pady=4)

        tk.Button(window, text="BAT LED  (LED_ON)",  bg="#27ae60", fg="white",
                  width=30, command=lambda: self.send("LED_ON")).pack(pady=6)

        tk.Button(window, text="TAT LED  (LED_OFF)", bg="#c0392b", fg="white",
                  width=30, command=lambda: self.send("LED_OFF")).pack(pady=4)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((PC_IP, PORT))

        Thread(target=self.listen, daemon=True).start()

    def send(self, msg):
        self.sock.sendto(msg.encode(), (BBB_IP, PORT))
        print(f"[PC -> BBB] {msg}")

    def listen(self):
        while True:
            try:
                data, _ = self.sock.recvfrom(1024)
                self.display.delete(1.0, tk.END)
                self.display.insert(tk.END, data.decode())
            except Exception:
                pass

if __name__ == "__main__":
    root = tk.Tk()
    BBB_Controller(root)
    root.mainloop()
```

---

## 6. Hướng dẫn Build & Triển khai

### Bước 1 — Kích hoạt package trong Buildroot

```bash
make menuconfig
# Vào: Target packages → Tìm và tích chọn:
#   [*] libbbb_net
#   [*] app_add_json
```

### Bước 2 — Build

```bash
# Build thư viện trước
make libbbb_net-rebuild

# Build ứng dụng
make app_add_json-build
```

### Bước 3 — Copy lên BBB

```bash
scp output/target/usr/bin/APP_ADD_JSON root@192.168.7.2:/root/
```

### Bước 4 — Chạy hệ thống

**Trên BBB (qua SSH):**

```bash
ssh root@192.168.7.2
chmod +x /root/APP_ADD_JSON
/root/APP_ADD_JSON
```

**Trên Ubuntu:**

```bash
python3 control_app.py
```

---

