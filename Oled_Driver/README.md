# SSD1306 OLED Kernel Module trên Buildroot

Driver kernel tối giản cho màn hình SSD1306 I2C OLED, đóng gói thành Buildroot package, expose `/dev/oled` cho user-space.

---

## Mục tiêu

- Viết kernel module minimal driver cho SSD1306 qua I2C
- Expose `/dev/oled` để user-space ghi dữ liệu trực tiếp
- Đóng gói thành Buildroot package (`.ko`), **không sửa kernel source**
- Target platform: **BeagleBone Black**

---

## Yêu cầu

- Buildroot đã clone và cấu hình `beaglebone_defconfig`
- BeagleBone Black (hoặc board tương tự dùng OMAP I2C)
- SSD1306 OLED module kết nối qua I2C2 (địa chỉ `0x3C`)

---

## Bước 1 — Bật I2C trong Kernel

```bash
make linux-menuconfig
```

Bật các option sau:

```
Device Drivers --->
    I2C support --->
        <*> I2C device interface
        I2C Hardware Bus support --->
            <*> OMAP I2C adapter
```

---

## Bước 2 — Thêm OLED vào Device Tree

Mở file DTS của BeagleBone Black:

```
output/build/linux-6.16.5/arch/arm/boot/dts/ti/omap/am335x-boneblack.dts
```

Thêm node OLED vào `i2c2`:

```dts
&i2c2 {
    status = "okay";

    oled@3c {
        compatible = "solomon,ssd1306";
        reg = <0x3c>;
    };
};
```

> **Lưu ý:** Khi dùng Buildroot, DTS được build cùng kernel — chỉ cần sửa file `.dts` rồi `make` lại.

---

## Bước 3 — Tạo Buildroot Package

### Cấu trúc thư mục

```
package/oled_driver/
├── Config.in
├── oled_driver.mk
└── src/
    ├── ssd1306.c
    └── Makefile
```

### `Config.in`

```kconfig
config BR2_PACKAGE_OLED_DRIVER
    bool "oled_driver"
    depends on BR2_LINUX_KERNEL
    help
      Minimal SSD1306 kernel module exposing /dev/oled
```

Thêm vào `package/Config.in` tổng:

```kconfig
source "package/oled_driver/Config.in"
```

### `src/Makefile`

```makefile
obj-m += ssd1306.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

**Giải thích:**

| Dòng | Ý nghĩa |
|---|---|
| `obj-m += ssd1306.o` | Khai báo `ssd1306.c` sẽ được build thành kernel module (`.ko`) |
| `$(MAKE) -C $(KDIR)` | Gọi build system của kernel tại đường dẫn `KDIR` (kernel source) |
| `M=$(PWD) modules` | Chỉ định thư mục chứa module source, build target `modules` |
| `M=$(PWD) clean` | Dọn dẹp các file build trung gian (`.o`, `.mod`, ...) |

> `KDIR` được Buildroot truyền vào tự động thông qua `oled_driver.mk`, không cần set thủ công khi build trong Buildroot. Chỉ cần set `KDIR` thủ công khi build ngoài Buildroot.

### `src/ssd1306.c`

```c
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "oled"

static struct i2c_client *oled_client;
static int major;

static ssize_t oled_write(struct file *file,
                          const char __user *buf,
                          size_t len, loff_t *off)
{
    u8 data[32];
    int i, ret;

    if (len > sizeof(data))
        len = sizeof(data);

    if (copy_from_user(data, buf, len))
        return -EFAULT;

    for (i = 0; i < len; i++) {
        ret = i2c_smbus_write_byte(oled_client, data[i]);
        if (ret < 0)
            return ret;
    }

    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = oled_write,
};

static int ssd1306_probe(struct i2c_client *client)
{
    printk("SSD1306 minimal driver\n");
    oled_client = client;
    major = register_chrdev(0, DEVICE_NAME, &fops);
    return 0;
}

static void ssd1306_remove(struct i2c_client *client)
{
    unregister_chrdev(major, DEVICE_NAME);
}

static const struct of_device_id ssd1306_of_match[] = {
    { .compatible = "solomon,ssd1306" },
    {}
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static struct i2c_driver ssd1306_driver = {
    .driver = {
        .name = "ssd1306",
        .of_match_table = ssd1306_of_match,
    },
    .probe  = ssd1306_probe,
    .remove = ssd1306_remove,
};

module_i2c_driver(ssd1306_driver);

MODULE_LICENSE("GPL");
```

### `oled_driver.mk`

```makefile
OLED_DRIVER_VERSION     = 1.0
OLED_DRIVER_SITE        = $(TOPDIR)/package/oled_driver/src
OLED_DRIVER_SITE_METHOD = local

define OLED_DRIVER_BUILD_CMDS
    $(MAKE) -C $(LINUX_DIR) \
        M=$(@D) \
        ARCH=$(KERNEL_ARCH) \
        CROSS_COMPILE=$(TARGET_CROSS) \
        modules
endef

define OLED_DRIVER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/ssd1306.ko \
        $(TARGET_DIR)/lib/modules/ssd1306.ko
endef

$(eval $(generic-package))
```

---

## Bước 4 — Build

Vào menuconfig, chọn package:

```
Target packages → oled_driver → [*]
```

Sau đó build:

```bash
make
```

> Sau khi build: `ssd1306.ko` sẽ được cài vào rootfs tại `/lib/modules/ssd1306.ko`.

---

## Bước 5 — Tích hợp với Library và Application

`ssd1306.ko` đóng vai trò **thin driver** — chỉ nhận raw bytes từ user-space và forward xuống I2C. Toàn bộ logic hiển thị (init, clear, draw text, draw pixel, ...) nằm ở user-space.

### Kiến trúc

```
oled_app.c          ← application: logic hiển thị, điều khiển nội dung
     │  gọi hàm
     ▼
oled_lib.c/.h       ← library: đóng gói SSD1306 commands (init, draw, ...)
     │  write()
     ▼
/dev/oled           ← char device (tạo bởi ssd1306.ko)
     │
ssd1306.ko          ← kernel module: forward bytes xuống I2C
     │  i2c_smbus_write_byte()
     ▼
SSD1306 OLED Hardware
```

### `oled_lib.h`

```c
#ifndef OLED_LIB_H
#define OLED_LIB_H

#include <stdint.h>

int  oled_open(const char *dev);                     // mở /dev/oled, trả về fd
void oled_init(int fd);                              // gửi chuỗi lệnh khởi tạo SSD1306
void oled_clear(int fd);                             // xóa toàn bộ màn hình
void oled_write_cmd(int fd, uint8_t cmd);            // gửi 1 command byte
void oled_write_data(int fd, uint8_t *buf, int len); // gửi data bytes
void oled_close(int fd);

#endif
```

### `oled_lib.c`

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "oled_lib.h"

// Chuỗi khởi tạo cơ bản cho SSD1306 128x64
static uint8_t init_cmds[] = {
    0x00,         // Co=0, D/C#=0 → command stream
    0xAE,         // Display OFF
    0xD5, 0x80,   // Set display clock
    0xA8, 0x3F,   // Set multiplex ratio (64)
    0xD3, 0x00,   // Set display offset
    0x40,         // Set start line = 0
    0x8D, 0x14,   // Charge pump enable
    0x20, 0x00,   // Memory mode: horizontal
    0xA1,         // Segment remap
    0xC8,         // COM scan direction
    0xDA, 0x12,   // COM pins config
    0x81, 0xCF,   // Set contrast
    0xD9, 0xF1,   // Pre-charge period
    0xDB, 0x40,   // VCOMH deselect level
    0xA4,         // Entire display ON (follow RAM)
    0xA6,         // Normal display (not inverted)
    0xAF,         // Display ON
};

int oled_open(const char *dev) {
    return open(dev, O_WRONLY);
}

void oled_init(int fd) {
    write(fd, init_cmds, sizeof(init_cmds));
}

void oled_write_cmd(int fd, uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = command byte prefix
    write(fd, buf, 2);
}

void oled_write_data(int fd, uint8_t *buf, int len) {
    write(fd, buf, len);
}

void oled_clear(int fd) {
    uint8_t blank[128] = {0};
    for (int page = 0; page < 8; page++) {
        oled_write_cmd(fd, 0xB0 + page); // set page address
        oled_write_cmd(fd, 0x00);        // set column low nibble
        oled_write_cmd(fd, 0x10);        // set column high nibble
        oled_write_data(fd, blank, sizeof(blank));
    }
}

void oled_close(int fd) {
    close(fd);
}
```

### `oled_app.c`

```c
#include <stdio.h>
#include "oled_lib.h"

int main(void) {
    int fd = oled_open("/dev/oled");
    if (fd < 0) {
        perror("open /dev/oled");
        return 1;
    }

    oled_init(fd);
    oled_clear(fd);

    // Ghi dữ liệu hiển thị tuỳ ý tại đây:
    // - vẽ pixel bitmap
    // - render text
    // - hiển thị icon, ...

    oled_close(fd);
    return 0;
}
```

### Load module trên target

```bash
insmod /lib/modules/ssd1306.ko
dmesg | grep SSD
# SSD1306 minimal driver
```

---

## Kiến trúc tổng quan

```
oled_app.c  (application)
   │  gọi oled_lib API
   ▼
oled_lib.c  (user-space library)
   │  write()
   ▼
/dev/oled   (char device)
   │
ssd1306.ko  (kernel module)
   │  i2c_smbus_write_byte()
   ▼
I2C Bus (i2c2)
   │
SSD1306 OLED Hardware
```

---

## Tóm tắt

| Thành phần | Vai trò |
|---|---|
| `ssd1306.ko` | Kernel driver, expose `/dev/oled`, forward bytes xuống I2C |
| Device Tree | Khai báo device `solomon,ssd1306` trên I2C2 |
| Buildroot package | Build và cài `.ko` vào rootfs, không sửa kernel source |
| `oled_lib.c` | Thư viện user-space: đóng gói SSD1306 commands |
| `oled_app.c` | Application: điều khiển logic nội dung hiển thị |
