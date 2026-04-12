# nRF24L01 on BeagleBone Black — Buildroot Guide

---

## Mục lục

### Phần 1 — Kernel Driver
1. [Cấu trúc thư mục](#1-cấu-trúc-thư-mục)
2. [Cấu hình Kernel](#2-cấu-hình-kernel)
3. [Device Tree](#3-device-tree)
4. [Config.in](#4-configin)
5. [nrf24_driver.mk](#5-nrf24_drivermk)
6. [src/Makefile](#6-srcmakefile)
7. [src/nrf24l01.c — kernel driver](#7-srcnrf24l01c--kernel-driver)
8. [Thêm vào package/Config.in tổng](#8-thêm-vào-packageconfigin-tổng)
9. [Chọn package & Build](#9-chọn-package--build)
10. [Test trên board](#10-test-trên-board)

### Phần 2 — Userspace Library
11. [Cấu trúc thư mục](#11-cấu-trúc-thư-mục-1)
12. [Config.in](#12-configin-1)
13. [nrf24.mk](#13-nrf24mk)
14. [src/nrf_spi/nrf_spi.h](#14-srcnrf_spinrf_spih)
15. [src/nrf_spi/nrf_spi.c](#15-srcnrf_spinrf_spic)
16. [src/nrf24l01/nrf24l01.h](#16-srcnrf24l01nrf24l01h)
17. [src/nrf24l01/nrf24l01.c](#17-srcnrf24l01nrf24l01c)
18. [Thêm vào package/Config.in tổng](#18-thêm-vào-packageconfigin-tổng-1)
19. [Chọn package & Build](#19-chọn-package--build-1)

### Phần 3 — Application
20. [Cấu trúc thư mục](#20-cấu-trúc-thư-mục-2)
21. [Config.in](#21-configin-2)
22. [nrf24_app.mk](#22-nrf24_appmk)
23. [src/main.c — BBB RX app](#23-srcmainc--bbb-rx-app)
24. [src/main.cpp — ESP32 TX app](#24-srcmaincpp--esp32-tx-app)
25. [Thêm vào package/Config.in tổng](#25-thêm-vào-packageconfigin-tổng-2)
26. [Chọn package & Build](#26-chọn-package--build-2)
27. [Test trên board](#27-test-trên-board-1)

---

# Phần 1 — Kernel Driver

## 1. Cấu trúc thư mục

```
package/nrf24_driver/
├── Config.in
├── nrf24_driver.mk
└── src/
    ├── nrf24l01.c
    └── Makefile
```

---

## 2. Cấu hình Kernel

```bash
make linux-menuconfig
```

Vào:
```
Device Drivers
  └── SPI support (CONFIG_SPI=y)
        └── McSPI driver for OMAP (CONFIG_SPI_OMAP24XX=y)
```

Chọn `*` (built-in), không phải `M`.

Kiểm tra:
```bash
grep -E "CONFIG_SPI|CONFIG_SPI_OMAP" output/build/linux-*/.config
```

Kết quả đúng:
```
CONFIG_SPI=y
CONFIG_SPI_OMAP24XX=y
```

---

## 3. Device Tree

File: `output/build/linux-<ver>/arch/arm/boot/dts/ti/omap/am335x-boneblack.dts`

Thêm vào cuối file:

```dts
&am33xx_pinmux {
    spi0_pins: spi0-pins {
        pinctrl-single,pins = <
            0x150 0x28 0x00  /* P9_22 SCLK  - PIN_INPUT  MODE0 */
            0x154 0x28 0x00  /* P9_21 MISO  - PIN_INPUT  MODE0 */
            0x158 0x08 0x00  /* P9_18 MOSI  - PIN_OUTPUT MODE0 */
            0x15c 0x08 0x00  /* P9_17 CS0   - PIN_OUTPUT MODE0 */
        >;
    };
};

&spi0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&spi0_pins>;
    nrf24@0 {
        compatible = "nordic,nrf24l01";
        reg = <0>;                                /* CSN = P9_17 (CS0) */
        spi-max-frequency = <8000000>;
        ce-gpios = <&gpio1 16 GPIO_ACTIVE_HIGH>;  /* CE  = P9_15 */
    };
};
```

### Pinout SPI0 trên BBB

| Signal | Pin   | GPIO     |
|--------|-------|----------|
| SCLK   | P9_22 | GPIO0_2  |
| MOSI   | P9_18 | GPIO0_4  |
| MISO   | P9_21 | GPIO0_3  |
| CSN    | P9_17 | GPIO0_5  |
| CE     | P9_15 | GPIO1_16 |

> CSN cắm vào P9_17 — SPI controller tự kéo qua CS0. CE cắm vào P9_15 — driver kéo qua GPIO ioctl.

Sau khi sửa DTS phải rebuild:
```bash
rm output/build/linux-<ver>/arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb
make linux-rebuild
make
```

---

## 4. Config.in

`package/nrf24_driver/Config.in`

```
config BR2_PACKAGE_NRF24_DRIVER
    bool "nrf24_driver"
    depends on BR2_LINUX_KERNEL
    help
      Minimal nRF24L01 SPI driver exposing /dev/nrf24
```

---

## 5. nrf24_driver.mk

`package/nrf24_driver/nrf24_driver.mk`

```makefile
NRF24_DRIVER_VERSION     = 1.0
NRF24_DRIVER_SITE        = $(TOPDIR)/package/nrf24_driver/src
NRF24_DRIVER_SITE_METHOD = local
NRF24_DRIVER_LICENSE     = GPL-2.0

define NRF24_DRIVER_BUILD_CMDS
    $(MAKE) -C $(LINUX_DIR) \
        M=$(@D) \
        ARCH=$(KERNEL_ARCH) \
        CROSS_COMPILE=$(TARGET_CROSS) \
        modules
endef

define NRF24_DRIVER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/nrf24l01.ko \
        $(TARGET_DIR)/lib/modules/nrf24l01.ko
endef

$(eval $(generic-package))
```

---

## 6. src/Makefile

`package/nrf24_driver/src/Makefile`

```makefile
obj-m += nrf24l01.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

---

## 7. src/nrf24l01.c — kernel driver

`package/nrf24_driver/src/nrf24l01.c`

```c
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/ioctl.h>

#define DEVICE_NAME     "nrf24"
#define NRF_IOC_MAGIC   'n'
#define NRF_IOC_CE_HIGH   _IO(NRF_IOC_MAGIC, 0)
#define NRF_IOC_CE_LOW    _IO(NRF_IOC_MAGIC, 1)

static struct spi_device *nrf24_spi;
static struct gpio_desc  *ce_gpio;
static int                major;
static struct class      *nrf24_class;
static struct device     *nrf24_device;

static u8     last_rx[64];
static size_t last_rx_len;

/* ── core transfer ───────────────────────────────────────── */

static int nrf24_transfer(const u8 *tx_buf, u8 *rx_buf, size_t len)
{
    struct spi_transfer t = {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len    = len,
    };
    struct spi_message m;
    int ret;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    ret = spi_sync(nrf24_spi, &m);
    return ret;
}

/* ── fops ────────────────────────────────────────────────── */

static ssize_t nrf24_write(struct file *file, const char __user *buf,
                           size_t len, loff_t *off)
{
    u8 tx[64];
    int ret;

    if (len > sizeof(tx)) len = sizeof(tx);
    if (copy_from_user(tx, buf, len)) return -EFAULT;

    ret = nrf24_transfer(tx, last_rx, len);
    if (ret < 0) return ret;

    last_rx_len = len;
    return (ssize_t)len;
}

static ssize_t nrf24_read(struct file *file, char __user *buf,
                          size_t len, loff_t *off)
{
    if (len > last_rx_len) len = last_rx_len;
    if (copy_to_user(buf, last_rx, len)) return -EFAULT;
    return (ssize_t)len;
}

static long nrf24_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case NRF_IOC_CE_HIGH:   gpiod_set_value(ce_gpio, 1); break;
    case NRF_IOC_CE_LOW:    gpiod_set_value(ce_gpio, 0); break;
    default: return -EINVAL;
    }
    return 0;
}

static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .write          = nrf24_write,
    .read           = nrf24_read,
    .unlocked_ioctl = nrf24_ioctl,
};

/* ── probe / remove ──────────────────────────────────────── */

static int nrf24_probe(struct spi_device *spi)
{
    nrf24_spi = spi;

    ce_gpio = devm_gpiod_get(&spi->dev, "ce", GPIOD_OUT_LOW);
    if (IS_ERR(ce_gpio)) {
        dev_err(&spi->dev, "get ce-gpio failed: %ld\n", PTR_ERR(ce_gpio));
        return PTR_ERR(ce_gpio);
    }

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        dev_err(&spi->dev, "register_chrdev failed: %d\n", major);
        return major;
    }

    nrf24_class = class_create(DEVICE_NAME);
    if (IS_ERR(nrf24_class)) {
        dev_err(&spi->dev, "class_create failed: %ld\n", PTR_ERR(nrf24_class));
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(nrf24_class);
    }

    nrf24_device = device_create(nrf24_class, NULL,
                                 MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(nrf24_device)) {
        dev_err(&spi->dev, "device_create failed: %ld\n", PTR_ERR(nrf24_device));
        class_destroy(nrf24_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(nrf24_device);
    }

    dev_info(&spi->dev, "nrf24 probe OK, major=%d\n", major);
    return 0;
}

static void nrf24_remove(struct spi_device *spi)
{
    device_destroy(nrf24_class, MKDEV(major, 0));
    class_destroy(nrf24_class);
    unregister_chrdev(major, DEVICE_NAME);
    dev_info(&spi->dev, "nrf24 removed\n");
}

/* ── driver registration ─────────────────────────────────── */

static const struct of_device_id nrf24_of_match[] = {
    { .compatible = "nordic,nrf24l01" },
    {}
};
MODULE_DEVICE_TABLE(of, nrf24_of_match);

static struct spi_driver nrf24_driver = {
    .driver = {
        .name           = "nrf24l01",
        .of_match_table = nrf24_of_match,
    },
    .probe  = nrf24_probe,
    .remove = nrf24_remove,
};
module_spi_driver(nrf24_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("nRF24L01 SPI driver for BeagleBone Black");
MODULE_AUTHOR("rimuru");
```

#### ioctl commands

| Command         | Value         | Tác dụng  |
|-----------------|---------------|-----------|
| NRF_IOC_CE_HIGH | `_IO('n', 0)` | CE = HIGH |
| NRF_IOC_CE_LOW  | `_IO('n', 1)` | CE = LOW  |

> CSN được SPI controller tự kéo qua CS0 (P9_17) — không cần ioctl riêng.
> `write()` gửi tx và lưu rx vào `last_rx`, `read()` trả về `last_rx` từ transaction trước.

---

## 8. Thêm vào package/Config.in tổng

```
source "package/nrf24_driver/Config.in"
```

---

## 9. Chọn package & Build

```bash
make menuconfig
```

Tìm `/` → `nrf24_driver` → bật.

```bash
make nrf24_driver-build
make
```

---

## 10. Test trên board

```bash
insmod /lib/modules/nrf24l01.ko
dmesg | grep nrf24
ls /dev/nrf24
```

Kết quả đúng:
```
[  xx.xxxxxx] nrf24l01 spi0.0: nrf24 probe OK, major=248
```

Kiểm tra pinmux:
```bash
mount -t debugfs debugfs /sys/kernel/debug
cat /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins | grep -E "pin 84|pin 85|pin 86|pin 87"
```

Kết quả đúng:
```
pin 84 ... 00000028
pin 85 ... 00000028
pin 86 ... 00000008
pin 87 ... 00000008
```

---
---

# Phần 2 — Userspace Library

## 11. Cấu trúc thư mục

```
package/nrf24/
├── Config.in
├── nrf24.mk
└── src/
    ├── nrf_spi/
    │   ├── nrf_spi.c
    │   └── nrf_spi.h
    └── nrf24l01/
        ├── nrf24l01.c
        └── nrf24l01.h
```

---

## 12. Config.in

`package/nrf24/Config.in`

```
config BR2_PACKAGE_NRF24
    bool "nrf24"
    help
      nRF24L01 userspace library (static + shared) for BeagleBone Black
```

---

## 13. nrf24.mk

`package/nrf24/nrf24.mk`

```makefile
NRF24_VERSION         = 1.0
NRF24_SITE            = $(TOPDIR)/package/nrf24/src
NRF24_SITE_METHOD     = local
NRF24_LICENSE         = MIT
NRF24_INSTALL_STAGING = YES

CFLAGS_NRF24 = $(TARGET_CFLAGS) -fPIC \
    -I$(@D) \
    -I$(@D)/nrf_spi \
    -I$(@D)/nrf24l01

define NRF24_BUILD_CMDS
    # Compile nrf_spi.c
    $(TARGET_CC) $(CFLAGS_NRF24) \
        -c $(@D)/nrf_spi/nrf_spi.c -o $(@D)/nrf_spi.o
    # Compile nrf24l01.c
    $(TARGET_CC) $(CFLAGS_NRF24) \
        -c $(@D)/nrf24l01/nrf24l01.c -o $(@D)/nrf24l01.o
    # Static library
    $(TARGET_AR) rcs $(@D)/libnrf24.a \
        $(@D)/nrf_spi.o \
        $(@D)/nrf24l01.o
    # Shared library
    $(TARGET_CC) -shared -o $(@D)/libnrf24.so \
        $(@D)/nrf_spi.o \
        $(@D)/nrf24l01.o
endef

define NRF24_INSTALL_STAGING_CMDS
    $(INSTALL) -D -m 0644 $(@D)/libnrf24.a           $(STAGING_DIR)/usr/lib/libnrf24.a
    $(INSTALL) -D -m 0755 $(@D)/libnrf24.so          $(STAGING_DIR)/usr/lib/libnrf24.so
    $(INSTALL) -D -m 0644 $(@D)/nrf_spi/nrf_spi.h   $(STAGING_DIR)/usr/include/nrf_spi.h
    $(INSTALL) -D -m 0644 $(@D)/nrf24l01/nrf24l01.h  $(STAGING_DIR)/usr/include/nrf24l01.h
endef

define NRF24_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/libnrf24.so $(TARGET_DIR)/usr/lib/libnrf24.so
endef

$(eval $(generic-package))
```

---

## 14. src/nrf_spi/nrf_spi.h

`package/nrf24/src/nrf_spi/nrf_spi.h`

```c
#ifndef __NRF_SPI__
#define __NRF_SPI__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#if defined(TEST_STM32)
/* ── STM32 ──────────────────────────────────────────────── */
#include "stm32f10x.h"

#define CE_PIN  GPIO_Pin_3
#define CSN_PIN GPIO_Pin_4

#define NRF_CE_HIGH()   GPIOA->BSRR = CE_PIN
#define NRF_CE_LOW()    GPIOA->BRR  = CE_PIN
#define NRF_CSN_HIGH()  GPIOA->BSRR = CSN_PIN
#define NRF_CSN_LOW()   GPIOA->BRR  = CSN_PIN

#else
/* ── BeagleBone Black ───────────────────────────────────── */
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>

#define NRF_IOC_MAGIC    'n'
#define NRF_IOC_CE_HIGH  _IO(NRF_IOC_MAGIC, 0)
#define NRF_IOC_CE_LOW   _IO(NRF_IOC_MAGIC, 1)

extern int nrf_fd;

#define NRF_CE_HIGH()   do { int r = ioctl(nrf_fd, NRF_IOC_CE_HIGH, 0); printf("CE HIGH ret=%d\n", r); } while(0)
#define NRF_CE_LOW()    do { int r = ioctl(nrf_fd, NRF_IOC_CE_LOW,  0); printf("CE LOW  ret=%d\n", r); } while(0)
#define NRF_CSN_HIGH()  /* kernel handles via CS0 */
#define NRF_CSN_LOW()   /* kernel handles via CS0 */

#endif /* TEST_STM32 */

void    SPI_Open(void);
uint8_t SPI_Transfer(uint8_t data);
void    SPI_TransferBytes(const uint8_t *tx_buf, uint8_t *rx_buf, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __NRF_SPI__ */
```

---

## 15. src/nrf_spi/nrf_spi.c

`package/nrf24/src/nrf_spi/nrf_spi.c`

```c
#include "nrf_spi.h"
#include <stdio.h>

#if defined(TEST_STM32)
/* ── STM32 ──────────────────────────────────────────────── */

void SPI_Open(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1,  ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = CE_PIN | CSN_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    NRF_CE_LOW();
    NRF_CSN_HIGH();
}

uint8_t SPI_Transfer(uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE)  == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

void SPI_TransferBytes(const uint8_t *tx_buf, uint8_t *rx_buf, uint8_t len)
{
    while (len--) {
        uint8_t data = SPI_Transfer(*tx_buf++);
        if (rx_buf) {
            *rx_buf++ = data;
        }
    }
}

#else
/* ── BeagleBone Black ───────────────────────────────────── */
#include <stdint.h>

int nrf_fd = -1;

void SPI_Open(void)
{
    nrf_fd = open("/dev/nrf24", O_RDWR);
    if (nrf_fd < 0) {
        perror("open /dev/nrf24");
        return;
    }
    printf("nrf_fd = %d\n", nrf_fd);
    NRF_CE_LOW();
    NRF_CSN_HIGH();
}

void SPI_TransferBytes(const uint8_t *tx_buf, uint8_t *rx_buf, uint8_t len)
{
    int w = write(nrf_fd, tx_buf, len);
    if (w < 0) {
        perror("write /dev/nrf24");
        return;
    }

    if (rx_buf) {
        int r = read(nrf_fd, rx_buf, len);
        if (r < 0) {
            perror("read /dev/nrf24");
        }
    }
}

uint8_t SPI_Transfer(uint8_t data)
{
    uint8_t rx = 0;
    SPI_TransferBytes(&data, &rx, 1);
    return rx;
}

#endif /* TEST_STM32 */
```

---

## 16. src/nrf24l01/nrf24l01.h

`package/nrf24/src/nrf24l01/nrf24l01.h`

```c
#ifndef __NRF24L01__
#define __NRF24L01__
#ifdef __cplusplus
extern "C"{
#endif
#include <stdint.h>

#define ADDRESS_LENGTH  5
#define PACKET_SIZE     32

// SPI Commands
#define NRF_CMD_R_REGISTER         0x00
#define NRF_CMD_W_REGISTER         0x20
#define NRF_CMD_R_RX_PAYLOAD       0x61
#define NRF_CMD_W_TX_PAYLOAD       0xA0
#define NRF_CMD_FLUSH_TX           0xE1
#define NRF_CMD_FLUSH_RX           0xE2
#define NRF_CMD_REUSE_TX_PL        0xE3
#define NRF_CMD_R_RX_PL_WID        0x60
#define NRF_CMD_W_ACK_PAYLOAD      0xA8
#define NRF_CMD_W_TX_PAYLOAD_NOACK 0xB0
#define NRF_CMD_ACTIVATE           0x50
#define NRF_CMD_NOP                0xFF

// Register Addresses
#define NRF_REG_CONFIG             0x00
#define NRF_REG_EN_AA              0x01
#define NRF_REG_EN_RXADDR          0x02
#define NRF_REG_SETUP_AW           0x03
#define NRF_REG_SETUP_RETR         0x04
#define NRF_REG_RF_CH              0x05
#define NRF_REG_RF_SETUP           0x06
#define NRF_REG_STATUS             0x07
#define NRF_REG_OBSERVE_TX         0x08
#define NRF_REG_CD                 0x09
#define NRF_REG_RX_ADDR_P0         0x0A
#define NRF_REG_RX_ADDR_P1         0x0B
#define NRF_REG_RX_ADDR_P2         0x0C
#define NRF_REG_RX_ADDR_P3         0x0D
#define NRF_REG_RX_ADDR_P4         0x0E
#define NRF_REG_RX_ADDR_P5         0x0F
#define NRF_REG_TX_ADDR            0x10
#define NRF_REG_RX_PW_P0           0x11
#define NRF_REG_RX_PW_P1           0x12
#define NRF_REG_RX_PW_P2           0x13
#define NRF_REG_RX_PW_P3           0x14
#define NRF_REG_RX_PW_P4           0x15
#define NRF_REG_RX_PW_P5           0x16
#define NRF_REG_FIFO_STATUS        0x17
#define NRF_REG_DYNPD              0x1C
#define NRF_REG_FEATURE            0x1D

// CONFIG Register Bits
#define CONFIG_MASK_RX_DR          0x40
#define CONFIG_MASK_TX_DS          0x20
#define CONFIG_MASK_MAX_RT         0x10
#define CONFIG_EN_CRC              0x08
#define CONFIG_CRCO                0x04
#define CONFIG_PWR_UP              0x02
#define CONFIG_PRIM_RX             0x01

// EN_AA Register Bits
#define ENAA_P5                    0x20
#define ENAA_P4                    0x10
#define ENAA_P3                    0x08
#define ENAA_P2                    0x04
#define ENAA_P1                    0x02
#define ENAA_P0                    0x01

// EN_RXADDR Register Bits
#define ERX_P5                     0x20
#define ERX_P4                     0x10
#define ERX_P3                     0x08
#define ERX_P2                     0x04
#define ERX_P1                     0x02
#define ERX_P0                     0x01

// SETUP_AW Register Bits
#define AW_3_BYTES   0x01
#define AW_4_BYTES   0x02
#define AW_5_BYTES   0x03

// RF_SETUP Register Bits
#define RF_SETUP_CONT_WAVE        0x80
#define RF_SETUP_RF_DR_LOW        0x20
#define RF_SETUP_PLL_LOCK         0x10
#define RF_SETUP_RF_DR_HIGH       0x08
#define RF_SETUP_RF_PWR_MASK      0x06

// STATUS Register Bits
#define STATUS_RX_DR              0x40
#define STATUS_TX_DS              0x20
#define STATUS_MAX_RT             0x10
#define STATUS_RX_P_NO_MASK       0x0E
#define STATUS_TX_FULL            0x01

// FIFO_STATUS Register Bits
#define FIFO_TX_REUSE             0x40
#define FIFO_TX_FULL              0x20
#define FIFO_TX_EMPTY             0x10
#define FIFO_RX_FULL              0x02
#define FIFO_RX_EMPTY             0x01

// DYNPD Register Bits
#define DPL_P5                    0x20
#define DPL_P4                    0x10
#define DPL_P3                    0x08
#define DPL_P2                    0x04
#define DPL_P1                    0x02
#define DPL_P0                    0x01

// FEATURE Register Bits
#define EN_DPL                    0x04
#define EN_ACK_PAY                0x02
#define EN_DYN_ACK                0x01

void NRF_WriteCmd(uint8_t cmd, const uint8_t *value, uint8_t len);
void NRF_ReadCmd(uint8_t cmd, uint8_t *value, uint8_t len);

void NRF_WriteReg_WithOneBit(uint8_t reg, uint8_t bit, uint8_t value);
uint8_t NRF_ReadReg_WithOneBit(uint8_t reg, uint8_t bit);

void NRF_WriteReg_WithOneByte(uint8_t reg, uint8_t value);
uint8_t NRF_ReadReg_WithOneByte(uint8_t reg);

void NRF_WriteReg_WithMultiBytes(uint8_t reg, const uint8_t *data, uint8_t len);
void NRF_ReadReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len);

void NRF_TX_Mode_Init(const uint8_t *addr, const uint8_t channel);
void NRF_SendData(uint8_t *data, uint8_t len);
void NRF_Flush_TX(void);

void NRF_RX_Mode_Init(const uint8_t *addr, const uint8_t channel);
void NRF_StartListening(void);
void NRF_StopListening(void);
uint8_t NRF_DataReady(void);
void NRF_ReadData(uint8_t *data, uint8_t len);
void NRF_Flush_RX(void);

uint8_t NRF_ReadStatus(void);

#ifdef __cplusplus
}
#endif
#endif
```

---

## 17. src/nrf24l01/nrf24l01.c

`package/nrf24/src/nrf24l01/nrf24l01.c`

```c
#include "nrf24l01.h"
#include "nrf_spi.h"
#include <string.h>

#if defined(TEST_STM32)
#include "delay.h"
#else
#include <unistd.h>
#define delay_ms(x) usleep((x) * 1000)
#define delay_us(x) usleep(x)
#endif

void NRF_WriteCmd(uint8_t cmd, const uint8_t *value, uint8_t len)
{
    uint8_t tx[1 + PACKET_SIZE];

    tx[0] = cmd;
    if (len) {
        memcpy(tx + 1, value, len);
    }

    NRF_CSN_LOW();
    SPI_TransferBytes(tx, NULL, 1 + len);
    NRF_CSN_HIGH();
}

void NRF_ReadCmd(uint8_t cmd, uint8_t *value, uint8_t len)
{
    uint8_t tx[1 + PACKET_SIZE];
    uint8_t rx[1 + PACKET_SIZE];
    uint8_t i;

    tx[0] = cmd;
    for (i = 0; i < len; i++) {
        tx[1 + i] = NRF_CMD_NOP;
    }

    NRF_CSN_LOW();
    SPI_TransferBytes(tx, rx, 1 + len);
    NRF_CSN_HIGH();

    for (i = 0; i < len; i++) {
        value[i] = rx[1 + i];
    }
}

void NRF_WriteReg_WithOneByte(uint8_t reg, uint8_t value)
{
    NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, &value, 1);
}

uint8_t NRF_ReadReg_WithOneByte(uint8_t reg)
{
    uint8_t data;
    NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, &data, 1);
    return data;
}

void NRF_WriteReg_WithOneBit(uint8_t reg, uint8_t bit, uint8_t value)
{
    uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);
    reg_value = value ? (reg_value | bit) : (reg_value & ~bit);
    NRF_WriteReg_WithOneByte(reg, reg_value);
}

uint8_t NRF_ReadReg_WithOneBit(uint8_t reg, uint8_t bit)
{
    uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);
    return (reg_value & bit) && 1;
}

void NRF_WriteReg_WithMultiBytes(uint8_t reg, const uint8_t *data, uint8_t len)
{
    NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, data, len);
}

void NRF_ReadReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len)
{
    NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, data, len);
}

void NRF_Flush_RX(void)
{
    NRF_CSN_LOW();
    SPI_Transfer(NRF_CMD_FLUSH_RX);
    NRF_CSN_HIGH();
}

void NRF_Flush_TX(void)
{
    NRF_CSN_LOW();
    SPI_Transfer(NRF_CMD_FLUSH_TX);
    NRF_CSN_HIGH();
}

uint8_t NRF_ReadStatus(void)
{
    uint8_t status;
    NRF_CSN_LOW();
    status = SPI_Transfer(NRF_CMD_NOP);
    NRF_CSN_HIGH();
    return status;
}

void NRF_TX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
    SPI_Open();
    delay_ms(20);

    NRF_WriteReg_WithOneByte(NRF_REG_CONFIG,      CONFIG_EN_CRC | CONFIG_PWR_UP);
    NRF_WriteReg_WithOneByte(NRF_REG_EN_AA,        ENAA_P0);
    NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR,    ERX_P0);
    NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW,     (ADDRESS_LENGTH - 0x02));
    NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR,   0x3f);
    NRF_WriteReg_WithOneByte(NRF_REG_RF_CH,        channel & 0x7F);
    NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR,    addr, ADDRESS_LENGTH);
    NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH);
    NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0,     PACKET_SIZE);
    NRF_WriteReg_WithOneByte(NRF_REG_STATUS,       STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);
    NRF_Flush_TX();
    NRF_CE_LOW();
    delay_ms(10);
}

void NRF_SendData(uint8_t *data, uint8_t len)
{
    uint8_t temp[PACKET_SIZE];
    NRF_CE_LOW();
    NRF_Flush_TX();

    len = len > PACKET_SIZE ? PACKET_SIZE : len;
    memset(temp, 0, PACKET_SIZE);
    memmove(temp, data, len);
    NRF_WriteCmd(NRF_CMD_W_TX_PAYLOAD, temp, PACKET_SIZE);

    NRF_CE_HIGH();
    delay_us(50);
    NRF_CE_LOW();

    int timeout = 2000;
    while (!(NRF_ReadStatus() & (STATUS_TX_DS | STATUS_MAX_RT))) {
        usleep(1000);
        if (--timeout == 0) {
            printf("TIMEOUT STATUS=0x%02x\n", NRF_ReadStatus());
            NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 0x70);
            return;
        }
    }
    NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 0x70);
}

void NRF_RX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
    SPI_Open();
    delay_ms(20);

    NRF_WriteReg_WithOneByte(NRF_REG_CONFIG,      CONFIG_EN_CRC | CONFIG_PRIM_RX);
    NRF_WriteReg_WithOneByte(NRF_REG_EN_AA,        ENAA_P0);
    NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR,    ERX_P0);
    NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW,     (ADDRESS_LENGTH - 0x02));
    NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR,   0x3f);
    NRF_WriteReg_WithOneByte(NRF_REG_RF_CH,        channel & 0x7F);
    NRF_WriteReg_WithOneByte(NRF_REG_RF_SETUP,     0x0f);
    NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR,    addr, ADDRESS_LENGTH);
    NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH);
    NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0,     PACKET_SIZE);
    NRF_WriteReg_WithOneByte(NRF_REG_STATUS,       (STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT));
    NRF_WriteReg_WithOneBit(NRF_REG_CONFIG,        CONFIG_PWR_UP, 1);
    delay_ms(10);
    NRF_Flush_RX();
    NRF_StopListening();
}

void NRF_StartListening(void)
{
    NRF_CE_HIGH();
}

void NRF_StopListening(void)
{
    NRF_CE_LOW();
}

uint8_t NRF_DataReady(void)
{
    uint8_t status = NRF_ReadStatus();
    return status & STATUS_RX_DR;
}

void NRF_Read_RX_Payload(uint8_t *data, uint8_t len)
{
    NRF_ReadCmd(NRF_CMD_R_RX_PAYLOAD, data, len);
}

void NRF_ReadData(uint8_t *data, uint8_t len)
{
    uint8_t temp[PACKET_SIZE];
    NRF_Read_RX_Payload(temp, PACKET_SIZE);
    len = len > PACKET_SIZE ? PACKET_SIZE : len;
    memmove(data, temp, len);
    NRF_WriteReg_WithOneByte(NRF_REG_STATUS, STATUS_RX_DR);
}
```

---

## 18. Thêm vào package/Config.in tổng

```
source "package/nrf24/Config.in"
```

---

## 19. Chọn package & Build

```bash
make menuconfig
```

Tìm `/` → `nrf24` → bật.

```bash
make nrf24-build
make nrf24-install-staging
```

Kiểm tra staging:
```bash
ls output/staging/usr/lib/libnrf24*
ls output/staging/usr/include/nrf*
```

---
---

# Phần 3 — Application

## 20. Cấu trúc thư mục

```
package/nrf24_app/
├── Config.in
├── nrf24_app.mk
└── src/
    └── main.c          ← BBB (RX mode)

ESP32/
└── src/
    └── main.cpp        ← ESP32 (TX hoặc RX mode)
```

---

## 21. Config.in

`package/nrf24_app/Config.in`

```
config BR2_PACKAGE_NRF24_APP
    bool "nrf24_app"
    depends on BR2_PACKAGE_NRF24
    help
      nRF24L01 test application for BeagleBone Black
```

---

## 22. nrf24_app.mk

`package/nrf24_app/nrf24_app.mk`

```makefile
NRF24_APP_VERSION     = 1.0
NRF24_APP_SITE        = $(TOPDIR)/package/nrf24_app/src
NRF24_APP_SITE_METHOD = local
NRF24_APP_DEPENDENCIES = nrf24

define NRF24_APP_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) \
        -I$(STAGING_DIR)/usr/include \
        $(@D)/main.c \
        -L$(STAGING_DIR)/usr/lib \
        -Wl,-Bstatic -lnrf24 -Wl,-Bdynamic \
        -o $(@D)/nrf24_app
endef

define NRF24_APP_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/nrf24_app \
        $(TARGET_DIR)/usr/bin/nrf24_app
endef

$(eval $(generic-package))
```

---

## 23. src/main.c — BBB RX app

`package/nrf24_app/src/main.c`

```c
#include "nrf24l01.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const uint8_t addr[]  = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t channel = 40;

int main(void)
{
    printf("nRF24L01 RX init...\n");
    NRF_RX_Mode_Init(addr, channel);
    NRF_StartListening();
    printf("Listening...\n");

    /* Debug: In ra các register quan trọng */
    printf("CONFIG=0x%02x\n",   NRF_ReadReg_WithOneByte(NRF_REG_CONFIG));
    printf("RF_SETUP=0x%02x\n", NRF_ReadReg_WithOneByte(NRF_REG_RF_SETUP));
    printf("RF_CH=0x%02x\n",    NRF_ReadReg_WithOneByte(NRF_REG_RF_CH));
    printf("STATUS=0x%02x\n",   NRF_ReadStatus());

    while (1) {
        uint8_t status = NRF_ReadStatus();
        if (status & STATUS_RX_DR) {
            printf("STATUS before read: 0x%02x\n", status);

            uint8_t data[PACKET_SIZE];
            memset(data, 0, PACKET_SIZE);
            NRF_ReadData(data, PACKET_SIZE);

            printf("Received: ");
            for (uint8_t i = 0; i < PACKET_SIZE; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");
            printf("Text: %s\n", (char *)data);
            printf("STATUS after read: 0x%02x\n", NRF_ReadStatus());
        }
        usleep(100000);
    }
    return 0;
}
```

---

## 24. src/main.cpp — ESP32 TX app

`ESP32/src/main.cpp`

PlatformIO `platformio.ini`:
```ini
[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200
lib_deps = nRF24/RF24
```

```cpp
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "nrf24l01.h"
#include "nrf_spi.h"

/* Chọn chế độ: */
#define ROLE_TX
// #define ROLE_RX

static const uint8_t addr[ADDRESS_LENGTH] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t channel = 40;

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }

#if defined(ROLE_TX)
    Serial.println("NRF24L01 ESP32 mode: TX");
#else
    Serial.println("NRF24L01 ESP32 mode: RX");
#endif

    SPI_Open();

#if defined(ROLE_TX)
    NRF_TX_Mode_Init(addr, channel);
    Serial.printf("CONFIG=0x%02x\n",   NRF_ReadReg_WithOneByte(NRF_REG_CONFIG));
    Serial.printf("RF_SETUP=0x%02x\n", NRF_ReadReg_WithOneByte(NRF_REG_RF_SETUP));
    Serial.printf("RF_CH=0x%02x\n",    NRF_ReadReg_WithOneByte(NRF_REG_RF_CH));
    Serial.printf("STATUS=0x%02x\n",   NRF_ReadStatus());
#else
    NRF_RX_Mode_Init(addr, channel);
    NRF_StartListening();
#endif
}

void loop() {
#if defined(ROLE_TX)
    uint8_t packet[PACKET_SIZE];
    memset(packet, 0, PACKET_SIZE);
    snprintf((char *)packet, PACKET_SIZE, "ESP32 packet %lu", millis() / 1000);
    Serial.printf("Sending: %s\n", (char *)packet);
    NRF_SendData(packet, PACKET_SIZE);
    Serial.println("Packet sent.");
    delay(1000);
#else
    if (NRF_DataReady()) {
        uint8_t data[PACKET_SIZE];
        memset(data, 0, PACKET_SIZE);
        NRF_ReadData(data, PACKET_SIZE);
        Serial.print("Received: ");
        for (uint8_t i = 0; i < PACKET_SIZE; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
        Serial.print("Text: ");
        Serial.println((char *)data);
    }
    delay(100);
#endif
}
```

> ESP32 dùng lại thư viện `nrf24l01.h/c` và `nrf_spi.h/c` — cần port `nrf_spi` cho ESP32 SPI API.

---

## 25. Thêm vào package/Config.in tổng

```
source "package/nrf24_app/Config.in"
```

---

## 26. Chọn package & Build

```bash
make menuconfig
```

Tìm `/` → `nrf24_app` → bật.

```bash
make nrf24_app-build
make
```

---

## 27. Test trên board

```bash
insmod /lib/modules/nrf24l01.ko
nrf24_app
```

Kết quả đúng khi nhận được data từ ESP32:
```
Listening...
CONFIG=0x0b
STATUS before read: 0x4e
Received: 45 53 50 33 32 20 70 61 63 6B ...
Text: ESP32 packet 5
STATUS after read: 0x0e
```

---

## Ghi chú

- `TEST_STM32` không được define khi build với Buildroot → tự động dùng code BBB.
- Mỗi lần sửa DTS phải xóa DTB cũ và `make linux-rebuild` rồi flash lại `sdcard.img`.
- `NRF_CSN_HIGH/LOW` trên BBB là no-op — SPI controller tự kéo CS0 (P9_17).
- CE GPIO = gpio-528 (P9_15A) trên BBB kernel mới.
