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
&am33xx_pinmux {
    i2c2_pins: i2c2_pins {
        pinctrl-single,pins = 
            AM33XX_IOPAD(0x190, PIN_INPUT | MUX_MODE2) /* P9_19 I2C2_SDA */
            AM33XX_IOPAD(0x194, PIN_INPUT | MUX_MODE2) /* P9_20 I2C2_SCL */
        >;
    };
};
&i2c2 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&i2c2_pins>;
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
#include <linux/device.h> 
#define DEVICE_NAME "oled"
static struct i2c_client *oled_client;
static int major;
static struct class *oled_class;  
static struct device *oled_device;

static ssize_t oled_write(struct file *file, const char __user *buf, size_t len, loff_t *off){
    u8 data[32]; 
    int ret;
    if (len > sizeof(data))
        len = sizeof(data);
    if (copy_from_user(data, buf, len))
        return -EFAULT;
    ret = i2c_master_send(oled_client, data, len);
    if (ret < 0)
        return ret;
    return len;
}
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = oled_write,
};

static int ssd1306_probe(struct i2c_client *client)
{
    oled_client = client;
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0)
        return major;
    oled_class = class_create(DEVICE_NAME);
    if (IS_ERR(oled_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(oled_class);
    }
    oled_device = device_create(oled_class, NULL,
                                MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(oled_device)) {
        class_destroy(oled_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(oled_device);
    }
    printk("SSD1306 probe OK, major=%d\n", major);
    return 0;
}
static void ssd1306_remove(struct i2c_client *client)
{
    device_destroy(oled_class, MKDEV(major, 0));
    class_destroy(oled_class);
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
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
};
module_i2c_driver(ssd1306_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSD1306 OLED I2C minimal driver");
MODULE_AUTHOR("rimuru");
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

## Bước 5 — VD sử dụng hàm write của driver oled1306

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



### `oled_app.c`

```c
#include <stdio.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

// Font 5x7 cơ bản (chỉ chứa chữ cần dùng A-Z, space)
static const uint8_t font5x7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
    ['a'] = {0x20, 0x54, 0x54, 0x54, 0x78},
    ['b'] = {0x7F, 0x48, 0x44, 0x44, 0x38},
    ['c'] = {0x38, 0x44, 0x44, 0x44, 0x20},
    ['d'] = {0x38, 0x44, 0x44, 0x48, 0x7F},
    ['e'] = {0x38, 0x54, 0x54, 0x54, 0x18},
    ['f'] = {0x08, 0x7E, 0x09, 0x01, 0x02},
    ['g'] = {0x0C, 0x52, 0x52, 0x52, 0x3E},
    ['h'] = {0x7F, 0x08, 0x04, 0x04, 0x78},
    ['i'] = {0x00, 0x44, 0x7D, 0x40, 0x00},
    ['j'] = {0x20, 0x40, 0x44, 0x3D, 0x00},
    ['k'] = {0x7F, 0x10, 0x28, 0x44, 0x00},
    ['l'] = {0x00, 0x41, 0x7F, 0x40, 0x00},
    ['m'] = {0x7C, 0x04, 0x18, 0x04, 0x78},
    ['n'] = {0x7C, 0x08, 0x04, 0x04, 0x78},
    ['o'] = {0x38, 0x44, 0x44, 0x44, 0x38},
    ['p'] = {0x7C, 0x14, 0x14, 0x14, 0x08},
    ['q'] = {0x08, 0x14, 0x14, 0x18, 0x7C},
    ['r'] = {0x7C, 0x08, 0x04, 0x04, 0x08},
    ['s'] = {0x48, 0x54, 0x54, 0x54, 0x20},
    ['t'] = {0x04, 0x3F, 0x44, 0x40, 0x20},
    ['u'] = {0x3C, 0x40, 0x40, 0x20, 0x7C},
    ['v'] = {0x1C, 0x20, 0x40, 0x20, 0x1C},
    ['w'] = {0x3C, 0x40, 0x30, 0x40, 0x3C},
    ['x'] = {0x44, 0x28, 0x10, 0x28, 0x44},
    ['y'] = {0x0C, 0x50, 0x50, 0x50, 0x3C},
    ['z'] = {0x44, 0x64, 0x54, 0x4C, 0x44},
};

int oled_write(int fd, uint8_t mode, uint8_t byte) {
    uint8_t buf[2];
    buf[0] = mode ? 0x40 : 0x00;
    buf[1] = byte;
    if (write(fd, buf, 2) != 2) {
        perror("write");
        return -1;
    }
    return 0;
}

void oled_init(int fd) {
    oled_write(fd, 0, 0xAE);
    oled_write(fd, 0, 0xD5);
    oled_write(fd, 0, 0x80);
    oled_write(fd, 0, 0xA8);
    oled_write(fd, 0, 0x3F);
    oled_write(fd, 0, 0xD3);
    oled_write(fd, 0, 0x00);
    oled_write(fd, 0, 0x40);
    oled_write(fd, 0, 0x8D);
    oled_write(fd, 0, 0x14);
    oled_write(fd, 0, 0x20);
    oled_write(fd, 0, 0x00);
    oled_write(fd, 0, 0xA1);
    oled_write(fd, 0, 0xC8);
    oled_write(fd, 0, 0xDA);
    oled_write(fd, 0, 0x12);
    oled_write(fd, 0, 0x81);
    oled_write(fd, 0, 0x7F);
    oled_write(fd, 0, 0xD9);
    oled_write(fd, 0, 0xF1);
    oled_write(fd, 0, 0xDB);
    oled_write(fd, 0, 0x40);
    oled_write(fd, 0, 0xA4);
    oled_write(fd, 0, 0xA6);
    oled_write(fd, 0, 0xAF);
}

void oled_clear(int fd) {
    for (int page = 0; page < 8; page++) {
        oled_write(fd, 0, 0xB0 + page);
        oled_write(fd, 0, 0x00);
        oled_write(fd, 0, 0x10);
        for (int col = 0; col < 128; col++) {
            oled_write(fd, 1, 0x00);
        }
    }
}

// Vẽ 1 ký tự tại page và cột x
void oled_draw_char(int fd, uint8_t page, uint8_t x, char c) {
    // Giới hạn index font
    if (c < ' ' || c > 'z') c = ' ';

    // Set vị trí
    oled_write(fd, 0, 0xB0 + page);         // page
    oled_write(fd, 0, (x & 0x0F));          // column low
    oled_write(fd, 0, 0x10 | (x >> 4));     // column high

    // Gửi 5 cột pixel của ký tự
    for (int i = 0; i < 5; i++) {
        oled_write(fd, 1, font5x7[(uint8_t)c][i]);
    }
    // 1 cột trống để cách chữ
    oled_write(fd, 1, 0x00);
}

// Vẽ chuỗi tại page, bắt đầu từ cột x
void oled_draw_string(int fd, uint8_t page, uint8_t x, const char *str) {
    while (*str) {
        oled_draw_char(fd, page, x, *str++);
        x += 6; // 5 pixel chữ + 1 pixel cách
        if (x >= 128) break;
    }
}

int main() {
    int fd = open("/dev/oled", O_RDWR);
    if (fd < 0) {
        perror("open /dev/oled");
        return 1;
    }

    oled_init(fd);
    oled_clear(fd);
    oled_draw_string(fd, 3, 10, "Hello Rimuru");

    close(fd);
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
