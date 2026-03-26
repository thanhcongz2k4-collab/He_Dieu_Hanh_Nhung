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

## Bước 5 — Viết thư viện 


### Cấu trúc thư mục đề xuất oled-font

```
package/oled-font/
├── Config.in
├── oled-font.mk
└── src/
    ├── oled-fonts.h
    ├── oled-fonts.c
```

### `src/oled_fonts.h`

```c
#ifndef __OLED_FONTS_H__
#define __OLED_FONTS_H__

#include <stdint.h>

#define DEFAULT_FONT  Serif_plain_10

typedef struct {
  uint16_t bitmapOffset;
  uint8_t  width;
  uint8_t  height;
  uint8_t  xAdvance;
  int8_t   xOffset;
  int8_t   yOffset;
} GFXglyph_t;

typedef struct {
  uint8_t  *bitmap;
  GFXglyph_t *glyph;
  uint8_t   first;
  uint8_t   last;
  uint8_t   yAdvance;
} GFXfont_t;

extern const GFXfont_t Serif_plain_10;

#endif

```

### `src/oled_fonts.c`

```c

#include "oled_fonts.h"

#define PROGMEM  

// Created by https://oleddisplay.squix.ch/ Consider a donation
// In case of problems make sure that you are using the font file with the correct version!
const uint8_t Serif_plain_10Bitmaps[] PROGMEM = {

	// Bitmap Data:
	0x00, // ' '
	0xAA,0x88, // '!'
	0xAA,0xA0, // '"'
	0x24,0x28,0x7E,0x28,0xFC,0x50,0x50, // '#'
	0x23,0xEA,0x1C,0x2A,0xA7,0x08, // '$'
	0xE4,0x52,0x2A,0x1F,0xE1,0x51,0x28,0x9C, // '%'
	0x30,0x24,0x10,0x16,0xE9,0xA4,0x61,0xEC, // '&'
	0xA8, // '''
	0x52,0x49,0x24,0x40, // '('
	0x91,0x24,0x94,0x80, // ')'
	0x22,0xA7,0x3E,0x20, // '*'
	0x10,0x10,0x10,0xFE,0x10,0x10,0x10, // '+'
	0x50, // ','
	0xE0, // '-'
	0x80, // '.'
	0x22,0x44,0x44,0x88, // '/'
	0x72,0x28,0xA2,0x8A,0x27,0x00, // '0'
	0x4C,0x44,0x44,0xE0, // '1'
	0x72,0x20,0x84,0x21,0x2F,0x80, // '2'
	0x72,0x20,0x8C,0x0A,0x27,0x00, // '3'
	0x10,0xC5,0x14,0xF8,0x43,0x80, // '4'
	0xFA,0x0F,0x02,0x0A,0x27,0x00, // '5'
	0x72,0x28,0x3C,0x8A,0x27,0x00, // '6'
	0xFA,0x21,0x04,0x20,0x84,0x00, // '7'
	0x72,0x28,0x9C,0xDA,0x27,0x00, // '8'
	0x72,0x28,0x9E,0x0A,0x27,0x00, // '9'
	0x82, // ':'
	0x40,0x28, // ';'
	0x04,0x73,0x03,0x80,0xC0, // '<'
	0xFC,0x03,0xF0, // '='
	0x80,0xE0,0x31,0xCC,0x00, // '>'
	0xE0,0x84,0xC4,0x01,0x00, // '?'
	0x1E,0x18,0x4D,0xAA,0x9A,0xA2,0xA8,0xAD,0xF1,0x80,0x3E,0x00, // '@'
	0x10,0x30,0x28,0x28,0x7C,0x44,0xCE, // 'A'
	0xF8,0x89,0x13,0xC4,0x48,0xBE,0x00, // 'B'
	0x38,0x8A,0x04,0x08,0x08,0x9E,0x00, // 'C'
	0xF8,0x44,0x42,0x42,0x42,0x44,0xF8, // 'D'
	0xFC,0x89,0x03,0xC4,0x08,0xBF,0x00, // 'E'
	0xFC,0x89,0x03,0xC4,0x08,0x38,0x00, // 'F'
	0x3C,0x42,0x80,0x80,0x86,0x42,0x3C, // 'G'
	0xE7,0x21,0x10,0x8F,0xC4,0x22,0x13,0x9C, // 'H'
	0xE4,0x44,0x44,0xE0, // 'I'
	0x71,0x08,0x42,0x10,0x94,0xC0, // 'J'
	0xEC,0x48,0x50,0x60,0x50,0x48,0xE6, // 'K'
	0xE0,0x81,0x02,0x04,0x08,0xBF,0x00, // 'L'
	0xE3,0x98,0xC5,0x51,0x54,0x49,0x12,0x4E,0x38, // 'M'
	0xC7,0x31,0x14,0x8B,0x44,0xE2,0x33,0x88, // 'N'
	0x38,0x44,0x82,0x82,0x82,0x44,0x38, // 'O'
	0xF8,0x89,0x13,0xC4,0x08,0x38,0x00, // 'P'
	0x38,0x44,0x82,0x82,0x82,0x44,0x38,0x08,0x04, // 'Q'
	0xF8,0x44,0x44,0x78,0x48,0x44,0xE2, // 'R'
	0x72,0x28,0x1C,0x0A,0x27,0x00, // 'S'
	0xFE,0x92,0x10,0x10,0x10,0x10,0x38, // 'T'
	0xE7,0x21,0x10,0x88,0x44,0x22,0x10,0xF0, // 'U'
	0xEE,0x44,0x44,0x28,0x28,0x10,0x10, // 'V'
	0xED,0xC9,0x91,0x4A,0x29,0x43,0x30,0x42,0x08,0x40, // 'W'
	0xEE,0x44,0x28,0x10,0x28,0x44,0xEE, // 'X'
	0xEE,0x44,0x28,0x10,0x10,0x10,0x38, // 'Y'
	0xFD,0x10,0x41,0x82,0x08,0xBF,0x00, // 'Z'
	0xD2,0x49,0x24,0xC0, // '['
	0x88,0x44,0x44,0x22, // '\'
	0xC9,0x24,0x92,0xC0, // ']'
	0x30,0x92,0x10, // '^'
	0xF8, // '_'
	0x88, // '`'
	0x60,0x4F,0x24,0xF8, // 'a'
	0xC1,0x04,0x1C,0x49,0x24,0xBC, // 'b'
	0x72,0x28,0x20,0x78, // 'c'
	0x30,0x41,0x1C,0x92,0x49,0x1E, // 'd'
	0x72,0x2F,0xA0,0x78, // 'e'
	0x72,0x11,0xC4,0x21,0x1C, // 'f'
	0x7A,0x49,0x24,0x70,0x4E,0x00, // 'g'
	0xC0,0x81,0x03,0xC4,0x89,0x12,0x76, // 'h'
	0x40,0xC4,0x44,0xE0, // 'i'
	0x20,0x62,0x22,0x22,0xE0, // 'j'
	0xC0,0x81,0x02,0xC5,0x0E,0x12,0x76, // 'k'
	0xC4,0x44,0x44,0x4E, // 'l'
	0xFF,0x12,0x44,0x91,0x24,0xED,0x80, // 'm'
	0xF8,0x91,0x22,0x4E,0xC0, // 'n'
	0x72,0x28,0xA2,0x70, // 'o'
	0xF1,0x24,0x92,0x71,0x0E,0x00, // 'p'
	0x7A,0x49,0x24,0x70,0x43,0x80, // 'q'
	0xF2,0x90,0x8E,0x00, // 'r'
	0xF4,0x18,0x2F,0x00, // 's'
	0x42,0x38,0x84,0x29,0xC0, // 't'
	0xD8,0x91,0x22,0x47,0xC0, // 'u'
	0xEC,0x91,0x21,0x83,0x00, // 'v'
	0xD6,0x54,0x54,0x28,0x28, // 'w'
	0xD9,0x42,0x14,0xD8, // 'x'
	0xCC,0x91,0x21,0x83,0x04,0x30,0x00, // 'y'
	0xFA,0x42,0x12,0xF8, // 'z'
	0x31,0x08,0x4C,0x10,0x84,0x30, // '{'
	0xAA,0xAA,0xA0, // '|'
	0xE0,0x82,0x08,0x18,0x82,0x08,0xE0 // '}'
};
const GFXglyph_t Serif_plain_10Glyphs[] PROGMEM = {
// bitmapOffset, width, height, xAdvance, xOffset, yOffset
	  {     0,   2,   1,   4,    0,   -1 }, // ' '
	  {     1,   2,   7,   5,    1,   -7 }, // '!'
	  {     3,   4,   3,   6,    1,   -7 }, // '"'
	  {     5,   8,   7,   9,    0,   -7 }, // '#'
	  {    12,   6,   8,   7,    0,   -7 }, // '$'
	  {    18,   9,   7,  11,    0,   -7 }, // '%'
	  {    26,   9,   7,  10,    0,   -7 }, // '&'
	  {    34,   2,   3,   4,    1,   -7 }, // '''
	  {    35,   3,   9,   5,    0,   -8 }, // '('
	  {    39,   3,   9,   5,    1,   -8 }, // ')'
	  {    43,   6,   5,   6,    0,   -7 }, // '*'
	  {    47,   8,   7,   9,    1,   -7 }, // '+'
	  {    54,   3,   2,   4,    0,   -1 }, // ','
	  {    55,   4,   1,   4,    0,   -3 }, // '-'
	  {    56,   2,   1,   4,    1,   -1 }, // '.'
	  {    57,   4,   8,   4,    0,   -7 }, // '/'
	  {    61,   6,   7,   7,    0,   -7 }, // '0'
	  {    67,   4,   7,   7,    1,   -7 }, // '1'
	  {    71,   6,   7,   7,    0,   -7 }, // '2'
	  {    77,   6,   7,   7,    0,   -7 }, // '3'
	  {    83,   6,   7,   7,    0,   -7 }, // '4'
	  {    89,   6,   7,   7,    0,   -7 }, // '5'
	  {    95,   6,   7,   7,    0,   -7 }, // '6'
	  {   101,   6,   7,   7,    0,   -7 }, // '7'
	  {   107,   6,   7,   7,    0,   -7 }, // '8'
	  {   113,   6,   7,   7,    0,   -7 }, // '9'
	  {   119,   2,   4,   4,    1,   -4 }, // ':'
	  {   120,   3,   5,   4,    0,   -4 }, // ';'
	  {   122,   7,   5,   9,    0,   -6 }, // '<'
	  {   127,   7,   3,   9,    0,   -5 }, // '='
	  {   130,   7,   5,   9,    0,   -6 }, // '>'
	  {   135,   5,   7,   6,    0,   -7 }, // '?'
	  {   140,  10,   9,  11,    0,   -7 }, // '@'
	  {   152,   8,   7,   8,    0,   -7 }, // 'A'
	  {   159,   7,   7,   8,    0,   -7 }, // 'B'
	  {   166,   7,   7,   8,    0,   -7 }, // 'C'
	  {   173,   8,   7,   9,    0,   -7 }, // 'D'
	  {   180,   7,   7,   8,    0,   -7 }, // 'E'
	  {   187,   7,   7,   8,    0,   -7 }, // 'F'
	  {   194,   8,   7,   9,    0,   -7 }, // 'G'
	  {   201,   9,   7,  10,    0,   -7 }, // 'H'
	  {   209,   4,   7,   5,    0,   -7 }, // 'I'
	  {   213,   5,   9,   5,   -1,   -7 }, // 'J'
	  {   219,   8,   7,   8,    0,   -7 }, // 'K'
	  {   226,   7,   7,   8,    0,   -7 }, // 'L'
	  {   233,  10,   7,  11,    0,   -7 }, // 'M'
	  {   242,   9,   7,  10,    0,   -7 }, // 'N'
	  {   250,   8,   7,   9,    0,   -7 }, // 'O'
	  {   257,   7,   7,   8,    0,   -7 }, // 'P'
	  {   264,   8,   9,   9,    0,   -7 }, // 'Q'
	  {   273,   8,   7,   9,    0,   -7 }, // 'R'
	  {   280,   6,   7,   7,    0,   -7 }, // 'S'
	  {   286,   8,   7,   9,    0,   -7 }, // 'T'
	  {   293,   9,   7,  10,    0,   -7 }, // 'U'
	  {   301,   8,   7,   8,    0,   -7 }, // 'V'
	  {   308,  11,   7,  12,    0,   -7 }, // 'W'
	  {   318,   8,   7,   9,    0,   -7 }, // 'X'
	  {   325,   8,   7,   8,    0,   -7 }, // 'Y'
	  {   332,   7,   7,   8,    0,   -7 }, // 'Z'
	  {   339,   3,   9,   5,    0,   -8 }, // '['
	  {   343,   4,   8,   4,    0,   -7 }, // '\'
	  {   347,   3,   9,   5,    1,   -8 }, // ']'
	  {   351,   7,   3,   9,    1,   -7 }, // '^'
	  {   354,   6,   1,   6,    0,    1 }, // '_'
	  {   355,   3,   2,   6,    1,   -8 }, // '`'
	  {   356,   6,   5,   6,    0,   -5 }, // 'a'
	  {   360,   6,   8,   7,    0,   -8 }, // 'b'
	  {   366,   6,   5,   7,    0,   -5 }, // 'c'
	  {   370,   6,   8,   6,    0,   -8 }, // 'd'
	  {   376,   6,   5,   7,    0,   -5 }, // 'e'
	  {   380,   5,   8,   5,    0,   -8 }, // 'f'
	  {   385,   6,   7,   6,    0,   -5 }, // 'g'
	  {   391,   7,   8,   7,    0,   -8 }, // 'h'
	  {   398,   4,   7,   4,    0,   -7 }, // 'i'
	  {   402,   4,   9,   4,   -1,   -7 }, // 'j'
	  {   407,   7,   8,   7,    0,   -8 }, // 'k'
	  {   414,   4,   8,   4,    0,   -8 }, // 'l'
	  {   418,  10,   5,  10,    0,   -5 }, // 'm'
	  {   425,   7,   5,   7,    0,   -5 }, // 'n'
	  {   430,   6,   5,   7,    0,   -5 }, // 'o'
	  {   434,   6,   7,   7,    0,   -5 }, // 'p'
	  {   440,   6,   7,   7,    0,   -5 }, // 'q'
	  {   446,   5,   5,   6,    0,   -5 }, // 'r'
	  {   450,   5,   5,   6,    0,   -5 }, // 's'
	  {   454,   5,   7,   5,    0,   -7 }, // 't'
	  {   459,   7,   5,   7,    0,   -5 }, // 'u'
	  {   464,   7,   5,   7,    0,   -5 }, // 'v'
	  {   469,   8,   5,  10,    0,   -5 }, // 'w'
	  {   474,   6,   5,   7,    0,   -5 }, // 'x'
	  {   478,   7,   7,   7,    0,   -5 }, // 'y'
	  {   485,   6,   5,   6,    0,   -5 }, // 'z'
	  {   489,   5,   9,   7,    1,   -8 }, // '{'
	  {   495,   2,  10,   4,    1,   -8 }, // '|'
	  {   498,   6,   9,   7,    1,   -8 } // '}'
};
const GFXfont_t Serif_plain_10 PROGMEM = {
(uint8_t  *)Serif_plain_10Bitmaps,(GFXglyph_t *)Serif_plain_10Glyphs,0x20, 0x7E, 13};

```

### `package/oled_font/Config.in`

```kconfig
config BR2_PACKAGE_OLED_FONT
    bool "oled-font"
    help
      Simple OLED font library (GFX style)
```

### `package/oled_font/oled_font.mk`

```makefile
OLED_FONT_VERSION = 1.0
OLED_FONT_SITE = $(TOPDIR)/package/oled-font/src
OLED_FONT_SITE_METHOD = local
OLED_FONT_INSTALL_STAGING = YES

define OLED_FONT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC -c $(@D)/oled_fonts.c -o $(@D)/oled_fonts.o
	$(TARGET_AR) rcs $(@D)/liboledfont.a $(@D)/oled_fonts.o
endef

define OLED_FONT_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/liboledfont.a \
		$(STAGING_DIR)/usr/lib/liboledfont.a

	$(INSTALL) -D -m 0644 $(@D)/oled_fonts.h \
		$(STAGING_DIR)/usr/include/oled_fonts.h
endef

define OLED_FONT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/liboledfont.a \
		$(TARGET_DIR)/usr/lib/liboledfont.a
endef

$(eval $(generic-package))

```

### Tích hợp vào Buildroot

- Thêm `source "package/oled-font/Config.in"` vào `package/Config.in`.
- Chọn `Target packages -> oled-font` trong `make menuconfig`.
- `make` để build và cài.

---

### Cấu trúc thư mục đề xuất oled_i2c

```
package/oled_i2c/
├── Config.in
├── oled_i2c.mk
└── src/
    ├── oled_i2c.h
    ├── oled_i2c.c
```

### `src/oled_i2c.h`

```c
#ifndef __OLED_I2C_H__
#define __OLED_I2C_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TEST_STM32)

#include "stm32f10x.h"

#define OLED_I2C_PORT        I2C2
#define OLED_I2C_ADDR        (0x3C << 1)

#else

#include <fcntl.h>
#include <unistd.h>

#define OLED_DEVICE "/dev/oled"

#endif

void Oled_Open(void);
void Oled_WriteCommand(uint8_t cmd);
void Oled_WriteData(uint8_t* buffer, size_t buff_size);

#ifdef __cplusplus
}
#endif

#endif


```

### `src/oled_i2c.c`

```c
#include "oled_i2c.h"

#if defined(TEST_STM32)

#define OLED_TIMEOUT 10000

static int IIC_OLED_TIMEOUT = 0;

#define WAIT_FLAG(FLAG)                          \
    do {                                         \
        IIC_OLED_TIMEOUT = OLED_TIMEOUT;         \
        while ((FLAG) && (IIC_OLED_TIMEOUT--));  \
        if (IIC_OLED_TIMEOUT <= 0) return;       \
    } while (0)

void Oled_Open(void) 
{
    GPIO_InitTypeDef GPIO_InitStruct;
    I2C_InitTypeDef I2C_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    I2C_DeInit(OLED_I2C_PORT);

    I2C_InitStruct.I2C_ClockSpeed = 400000;
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_Ack = I2C_Ack_Disable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(OLED_I2C_PORT, &I2C_InitStruct);
    I2C_Cmd(OLED_I2C_PORT, ENABLE);
}

void Oled_WriteCommand(uint8_t cmd) 
{
    while (I2C_GetFlagStatus(OLED_I2C_PORT, I2C_FLAG_BUSY));

    I2C_GenerateSTART(OLED_I2C_PORT, ENABLE);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_MODE_SELECT));

    I2C_Send7bitAddress(OLED_I2C_PORT, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));

    I2C_SendData(OLED_I2C_PORT, 0x00);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));

    I2C_SendData(OLED_I2C_PORT, cmd);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));

    I2C_GenerateSTOP(OLED_I2C_PORT, ENABLE);
}

void Oled_WriteData(uint8_t* buffer, size_t buff_size) 
{
    while (I2C_GetFlagStatus(OLED_I2C_PORT, I2C_FLAG_BUSY));

    I2C_GenerateSTART(OLED_I2C_PORT, ENABLE);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_MODE_SELECT));

    I2C_Send7bitAddress(OLED_I2C_PORT, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));

    I2C_SendData(OLED_I2C_PORT, 0x40);
    WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));

    for (size_t i = 0; i < buff_size; i++) {
        I2C_SendData(OLED_I2C_PORT, buffer[i]);
        WAIT_FLAG(!I2C_CheckEvent(OLED_I2C_PORT, I2C_EVENT_MASTER_BYTE_TRANSMITTING));
    }

    I2C_GenerateSTOP(OLED_I2C_PORT, ENABLE);
}

#else

#include <stdio.h>

static int fd = -1;

void Oled_Open(void) 
{
    fd = open(OLED_DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("open oled");
    }
}

void Oled_WriteCommand(uint8_t cmd) 
{
    if (fd < 0) return;

    uint8_t buf[2] = {0x00, cmd};
    if (write(fd, buf, 2) < 0) {
        perror("write command");
    }
}

void Oled_WriteData(uint8_t* buffer, size_t buff_size) 
{
    if (fd < 0) return;

    uint8_t tmp[buff_size + 1];
    tmp[0] = 0x40;

    for (size_t i = 0; i < buff_size; i++) {
        tmp[i + 1] = buffer[i];
    }

    if (write(fd, tmp, buff_size + 1) < 0) {
        perror("write data");
    }
}

#endif

```

### `package/oled_i2c/Config.in`

```kconfig
config BR2_PACKAGE_OLED_I2C
    bool "oled_i2c"
    help
      Simple OLED SSD1306 library for Linux and STM32

```

### `package/oled_i2c/oled_i2c.mk`

```makefile
OLED_I2C_VERSION = 1.0
OLED_I2C_SITE = $(TOPDIR)/package/oled_i2c/src
OLED_I2C_SITE_METHOD = local
OLED_I2C_INSTALL_STAGING = YES
define OLED_I2C_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC -c $(@D)/oled_i2c.c -o $(@D)/oled_i2c.o
	$(TARGET_AR) rcs $(@D)/liboled.a $(@D)/oled_i2c.o
	$(TARGET_CC) -shared -o $(@D)/liboled.so $(@D)/oled_i2c.o
endef

define OLED_I2C_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/liboled.a \
		$(STAGING_DIR)/usr/lib/liboled.a

	$(INSTALL) -D -m 0755 $(@D)/liboled.so \
		$(STAGING_DIR)/usr/lib/liboled.so

	$(INSTALL) -D -m 0644 $(@D)/oled_i2c.h \
		$(STAGING_DIR)/usr/include/oled_i2c.h
endef

define OLED_I2C_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/liboled.so \
		$(TARGET_DIR)/usr/lib/liboled.so
endef

$(eval $(generic-package))


```

### Tích hợp vào Buildroot

- Thêm `source "package/oled_i2c/Config.in"` vào `package/Config.in`.
- Chọn `Target packages -> oled_i2c` trong `make menuconfig`.
- `make` để build và cài.

---

### Cấu trúc thư mục đề xuất oled

```
package/oled/
├── Config.in
├── oled.mk
└── src/
    ├── oled.h
    ├── oled.c
```

### `src/oled.h`

```c
#ifndef __OLED_H__
#define __OLED_H__

#include <stddef.h>
#include <stdint.h>

// Sử dụng SPL cho STM32F103
#include "oled_i2c.h"
#include "oled_fonts.h"

// #define FLIP_IMG

#define OLED_INCLUDE_FONT_11x18

#ifdef OLED_X_OFFSET
#define OLED_X_OFFSET_LOWER (OLED_X_OFFSET & 0x0F)
#define OLED_X_OFFSET_UPPER ((OLED_X_OFFSET >> 4) & 0x07)
#else
#define OLED_X_OFFSET_LOWER 0x00
#define OLED_X_OFFSET_UPPER 0x00
#endif

// SSD1306 OLED height in pixels
#ifndef OLED_HEIGHT
#define OLED_HEIGHT          32
#endif

// SSD1306 width in pixels
#ifndef OLED_WIDTH
#define OLED_WIDTH           128
#endif

#ifndef OLED_BUFFER_SIZE
#define OLED_BUFFER_SIZE   (OLED_WIDTH * OLED_HEIGHT / 8)
#endif

// Enumeration for screen colors
typedef enum {
    Black = 0x00, // Black color, no pixel
    White = 0x01  // Pixel is set. Color depends on OLED
} Oled_Color_e;

// Struct to store transformations
typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t Initialized;
    uint8_t DisplayOn;
} Oled_t;

typedef struct {
    uint8_t x;
    uint8_t y;
} Oled_Cursor_t;

typedef struct{
  uint8_t width;
  uint8_t height;
} Oled_StringSize_t;

// Procedure definitions
void Oled_Init(void);
void Oled_UpdateScreen(void);

void Oled_Fill(Oled_Color_e color);
Oled_Cursor_t Oled_GetCursor(void);
void Oled_SetCursor(uint8_t x, uint8_t y);
void Oled_DrawPixel(uint8_t x, uint8_t y, Oled_Color_e color);
Oled_StringSize_t Oled_GetStringSize(char *str, const GFXfont_t *font);
void Oled_WriteChar(char c, const GFXfont_t *font, Oled_Color_e color);
void Oled_WriteString(const char *str, const GFXfont_t *font, Oled_Color_e color);
void Oled_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);
void Oled_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);
void Oled_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color);

#endif


```

### `src/oled.c`

```c
#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>  // For memcpy
#include <math.h>

// Screenbuffer
static uint8_t oled_buffer[OLED_BUFFER_SIZE];

// Screen object
static Oled_t Oled;

/* Initialize the oled screen */
void Oled_Init(void) 
{
    uint8_t mux_ratio = 0x3F;
    uint8_t com_pins_cfg = 0x12;

    if (OLED_HEIGHT == 32) {
        mux_ratio = 0x1F;
        com_pins_cfg = 0x02;
    }

    Oled_Open();
    for(volatile int i=0; i<0xfffff; i++);

    Oled_WriteCommand(0xAE);
    Oled_WriteCommand(0x20); // memory addressing mode
    Oled_WriteCommand(0x00); // horizontal
    Oled_WriteCommand(0xB0);
    Oled_WriteCommand(0xC8);
    Oled_WriteCommand(0x00);
    Oled_WriteCommand(0x10);
    Oled_WriteCommand(0x40);
    Oled_WriteCommand(0x81);
    Oled_WriteCommand(0x7F);
    Oled_WriteCommand(0xA1);
    Oled_WriteCommand(0xA6);
    Oled_WriteCommand(0xA8);
    Oled_WriteCommand(mux_ratio);
    Oled_WriteCommand(0xA4);
    Oled_WriteCommand(0xD3);
    Oled_WriteCommand(0x00);
    Oled_WriteCommand(0xD5);
    Oled_WriteCommand(0x80);
    Oled_WriteCommand(0xD9);
    Oled_WriteCommand(0xF1);
    Oled_WriteCommand(0xDA);
    Oled_WriteCommand(com_pins_cfg);
    Oled_WriteCommand(0xDB);
    Oled_WriteCommand(0x40);
    Oled_WriteCommand(0x8D);
    Oled_WriteCommand(0x14);
    Oled_WriteCommand(0xAF);
    
    // Clear screen
    Oled_Fill(Black);
    
    // Flush buffer to screen
    Oled_UpdateScreen();
    
    // Set default values for screen object
    Oled.CurrentX = 0;
    Oled.CurrentY = 0;
    
    Oled.Initialized = 1;
}

/* Fill the whole screen with the given color */
void Oled_Fill(Oled_Color_e color) 
{
    memset(oled_buffer, (color == Black) ? 0x00 : 0xFF, sizeof(oled_buffer));
}

/* Write the screenbuffer with changed to the screen */
void Oled_UpdateScreen(void) 
{
    // Write data to each page of RAM. Number of pages
    // depends on the screen height:
    //
    //  * 32px   ==  4 pages
    //  * 64px   ==  8 pages
    //  * 128px  ==  16 pages
    for(uint8_t page = 0; page < OLED_HEIGHT/8; page++) 
    {
        Oled_WriteCommand(0xB0 + page); // Set page address
        Oled_WriteCommand(0x00 + OLED_X_OFFSET_LOWER); // Set lower column address
        Oled_WriteCommand(0x10 + OLED_X_OFFSET_UPPER); // Set higher column address
        Oled_WriteData(&oled_buffer[OLED_WIDTH*page], OLED_WIDTH);
    }
}

/*
 * Draw one pixel in the screenbuffer
 * X => X Coordinate
 * Y => Y Coordinate
 * color => Pixel color
 */
void Oled_DrawPixel(uint8_t x, uint8_t y, Oled_Color_e color) 
{
    
#ifdef FLIP_IMG
    y = OLED_HEIGHT - 1 - y;
    x = OLED_WIDTH - 1 - x;
#endif
   
    if(x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        // Don't write outside the buffer
        return;
    }
   
    // Draw in the right color
    if(color == White) {
        oled_buffer[x + (y / 8) * OLED_WIDTH] |= 1 << (y % 8);
    } else { 
        oled_buffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
    }
}

Oled_StringSize_t Oled_GetStringSize(char *str, const GFXfont_t *font)
{
    Oled_StringSize_t str_size = {0, 0};
    int8_t y_offset = 0;
    
    while(*str)
    {
        if (*str < font->first || *str > font->last) 
				{
					str_size.width = 0;
					str_size.height = 0;
					return str_size;
				}

        GFXglyph_t *glyph = &font->glyph[*str - font->first];
        
        if(abs(glyph->height - abs(glyph->yOffset)))
        { 
            if(y_offset < abs(glyph->height - abs(glyph->yOffset))) y_offset = abs(glyph->height - abs(glyph->yOffset));
        }
        if(str_size.height < glyph->height) str_size.height = glyph->height;
        str_size.width += glyph->xAdvance;
        str++;
    }
    str_size.height += y_offset;
    return str_size;
}

/*
 * Draw 1 char to the screen buffer
 * ch       => char om weg te schrijven
 * Font     => Font waarmee we gaan schrijven
 * color    => Black or White
 */
void Oled_WriteChar(char c, const GFXfont_t *font, Oled_Color_e color) {
    if (c < font->first || c > font->last) return;

    GFXglyph_t *glyph = &font->glyph[c - font->first];
    uint8_t  *bitmap = font->bitmap;

    uint16_t bo = glyph->bitmapOffset;
    uint8_t  w  = glyph->width;
    uint8_t  h  = glyph->height;
    int8_t   xo = glyph->xOffset;
    int8_t   yo = glyph->yOffset;

    int16_t x = Oled.CurrentX;
    int16_t y = Oled.CurrentY;

    uint8_t bit = 0, bits = bitmap[bo];

    for (uint8_t yy = 0; yy < h; yy++) {
        for (uint8_t xx = 0; xx < w; xx++) {
            if (bit >= 8) {
                bits = bitmap[++bo];
                bit  = 0;
            }
            if (bits & 0x80) {
                uint8_t cx = x + xo + xx;
                uint8_t cy = y + yo + yy;
                
            #ifdef FLIP_IMG
                uint8_t tx = OLED_WIDTH - 1 - cx;
                uint8_t ty = OLED_HEIGHT - 1 - cy;
                
            #else
                uint8_t tx = cx;
                uint8_t ty = cy;
            #endif
   
                
                // Toggle color
                if(color == White && (oled_buffer[tx + (ty / 8) * OLED_WIDTH] & (1 << (ty % 8)))) 
                {
                    color = Black;
                } 
                else if(color == Black && !(oled_buffer[tx + (ty / 8) * OLED_WIDTH] & (1 << (ty % 8)))) 
                {
                    color = White;
                }
                Oled_DrawPixel(cx, cy, color);
            }
            bits <<= 1;
            bit++;
        }
    }

    // Tự động dịch con trỏ sang phải
    Oled.CurrentX += glyph->xAdvance;
}

/* Write full string to screenbuffer */
void Oled_WriteString(const char *str, const GFXfont_t *font, Oled_Color_e color) 
{
    while (*str) {
        if (*str == '\n') 
        {
            Oled.CurrentY += font->yAdvance;
            Oled.CurrentX  = 0;
        } 
        else if (*str >= font->first && *str <= font->last) 
        {
            Oled_WriteChar(*str, font, color);
        }
        str++;
    }
}


Oled_Cursor_t Oled_GetCursor(void)
{
    Oled_Cursor_t cursor = {Oled.CurrentX, Oled.CurrentY};
    return cursor;
}

/* Position the cursor */
void Oled_SetCursor(uint8_t x, uint8_t y) 
{
    Oled.CurrentX = x;
    Oled.CurrentY = y;
}

/* Draw line by Bresenhem's algorithm */
void Oled_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    
    Oled_DrawPixel(x2, y2, color);

    while((x1 != x2) || (y1 != y2)) {
        Oled_DrawPixel(x1, y1, color);
        error2 = error * 2;
        if(error2 > -deltaY) {
            error -= deltaY;
            x1 += signX;
        }
        
        if(error2 < deltaX) {
            error += deltaX;
            y1 += signY;
        }
    }
    return;
}

/* Draw a rectangle */
void Oled_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    Oled_Line(x1,y1,x2,y1,color);
    Oled_Line(x2,y1,x2,y2,color);
    Oled_Line(x2,y2,x1,y2,color);
    Oled_Line(x1,y2,x1,y1,color);

    return;
}

/* Draw a filled rectangle */
void Oled_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Oled_Color_e color) 
{
    uint8_t x_start = ((x1<=x2) ? x1 : x2);
    uint8_t x_end   = ((x1<=x2) ? x2 : x1);
    uint8_t y_start = ((y1<=y2) ? y1 : y2);
    uint8_t y_end   = ((y1<=y2) ? y2 : y1);

    for (uint8_t y= y_start; (y<= y_end)&&(y<OLED_HEIGHT); y++) {
        for (uint8_t x= x_start; (x<= x_end)&&(x<OLED_WIDTH); x++) {
            Oled_DrawPixel(x, y, color);
        }
    }
    return;
}

```

### `package/oled/Config.in`

```kconfig
config BR2_PACKAGE_OLED
    bool "oled"
    depends on BR2_PACKAGE_OLED_I2C
    depends on BR2_PACKAGE_OLED_FONT
    help
      OLED graphics library (SSD1306)

```

### `package/oled/oled_font.mk`

```makefile
OLED_VERSION = 1.0
OLED_SITE = $(TOPDIR)/package/oled/src
OLED_SITE_METHOD = local

OLED_DEPENDENCIES = oled_i2c oled-font
OLED_INSTALL_STAGING = YES

define OLED_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC \
		-I$(STAGING_DIR)/usr/include \
		-c $(@D)/oled.c -o $(@D)/oled.o

	$(TARGET_AR) rcs $(@D)/liboledgfx.a $(@D)/oled.o

	$(TARGET_CC) -shared -o $(@D)/liboledgfx.so $(@D)/oled.o \
		-L$(STAGING_DIR)/usr/lib -loled -loledfont
endef

define OLED_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/liboledgfx.a \
		$(STAGING_DIR)/usr/lib/liboledgfx.a

	$(INSTALL) -D -m 0755 $(@D)/liboledgfx.so \
		$(STAGING_DIR)/usr/lib/liboledgfx.so

	$(INSTALL) -D -m 0644 $(@D)/oled.h \
		$(STAGING_DIR)/usr/include/oled.h
endef

define OLED_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/liboledgfx.so \
		$(TARGET_DIR)/usr/lib/liboledgfx.so
endef

$(eval $(generic-package))


```

### Tích hợp vào Buildroot

- Thêm `source "package/oled/Config.in"` vào `package/Config.in`.
- Chọn `Target packages -> oled` trong `make menuconfig`.
- `make` để build và cài.

---






