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
7. [src/nrf24l01.c](#7-srcnrf24l01c)
8. [Thêm vào package/Config.in tổng](#8-thêm-vào-packageconfigin-tổng)
9. [Chọn package & Build](#9-chọn-package--build)
10. [Test trên board](#10-test-trên-board)

### Phần 2 — Userspace Library
11. [Cấu trúc thư mục](#11-cấu-trúc-thư-mục)
12. [Config.in](#12-configin)
13. [nrf24.mk](#13-nrf24mk)
14. [src/nrf_spi/nrf_spi.h](#14-srcnrf_spinrf_spih)
15. [src/nrf_spi/nrf_spi.c](#15-srcnrf_spinrf_spic)
16. [src/nrf24l01/nrf24l01.h](#16-srcnrf24l01nrf24l01h)
17. [src/nrf24l01/nrf24l01.c](#17-srcnrf24l01nrf24l01c)
18. [Thêm vào package/Config.in tổng](#18-thêm-vào-packageconfigin-tổng)
19. [Chọn package & Build](#19-chọn-package--build)

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
&spi0 {
    status = "okay";
    nrf24@0 {
        compatible = "nordic,nrf24l01";
        reg = <0>;                                /* CS0 = P9_17 */
        spi-max-frequency = <8000000>;
        ce-gpios  = <&gpio1 16 GPIO_ACTIVE_HIGH>; /* CE  = P9_15 */
        cs-gpios  = <&gpio1 28 GPIO_ACTIVE_LOW>;  /* CSN = P9_12 */
    };
};
```

### Pinout SPI0 trên BBB

| Signal | Pin   | GPIO     |
|--------|-------|----------|
| SCLK   | P9_22 | GPIO0_2  |
| MOSI   | P9_18 | GPIO0_4  |
| MISO   | P9_21 | GPIO0_3  |
| CS0    | P9_17 | GPIO0_5  |
| CE     | P9_15 | GPIO1_16 |
| CSN    | P9_12 | GPIO1_28 |

> CS0 (P9_17) do SPI controller tự quản lý. CSN và CE là GPIO thủ công điều khiển qua ioctl.

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

> `KDIR` được Buildroot truyền vào tự động qua `.mk`. Chỉ cần set thủ công khi build ngoài Buildroot.

---

## 7. src/nrf24l01.c

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
#define NRF_IOC_CSN_HIGH  _IO(NRF_IOC_MAGIC, 2)
#define NRF_IOC_CSN_LOW   _IO(NRF_IOC_MAGIC, 3)

static struct spi_device *nrf24_spi;
static struct gpio_desc  *ce_gpio;
static struct gpio_desc  *csn_gpio;
static int                major;
static struct class      *nrf24_class;
static struct device     *nrf24_device;

/* ── core transfer ───────────────────────────────────────── */

static int nrf24_transfer(const u8 *tx_buf, u8 *rx_buf, size_t len)
{
    struct spi_transfer t = {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len    = len,
    };
    struct spi_message m;
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spi_sync(nrf24_spi, &m);
}

/* ── fops ────────────────────────────────────────────────── */

static ssize_t nrf24_write(struct file *file, const char __user *buf,
                           size_t len, loff_t *off)
{
    u8 tx[64], rx[64];
    int ret;

    if (len > sizeof(tx)) len = sizeof(tx);
    if (copy_from_user(tx, buf, len)) return -EFAULT;

    ret = nrf24_transfer(tx, rx, len);
    return ret < 0 ? ret : (ssize_t)len;
}

static ssize_t nrf24_read(struct file *file, char __user *buf,
                          size_t len, loff_t *off)
{
    u8 tx[64], rx[64];
    int ret;

    if (len > sizeof(rx)) len = sizeof(rx);
    memset(tx, 0xFF, len);

    ret = nrf24_transfer(tx, rx, len);
    if (ret < 0) return ret;
    if (copy_to_user(buf, rx, len)) return -EFAULT;
    return (ssize_t)len;
}

static long nrf24_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case NRF_IOC_CE_HIGH:   gpiod_set_value(ce_gpio,  1); break;
    case NRF_IOC_CE_LOW:    gpiod_set_value(ce_gpio,  0); break;
    case NRF_IOC_CSN_HIGH:  gpiod_set_value(csn_gpio, 1); break;
    case NRF_IOC_CSN_LOW:   gpiod_set_value(csn_gpio, 0); break;
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

    csn_gpio = devm_gpiod_get(&spi->dev, "cs", GPIOD_OUT_HIGH);
    if (IS_ERR(csn_gpio)) {
        dev_err(&spi->dev, "get csn-gpio failed: %ld\n", PTR_ERR(csn_gpio));
        return PTR_ERR(csn_gpio);
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

Driver expose `/dev/nrf24` với:
- `write()` → gửi data ra MOSI
- `read()` → nhận data từ MISO (clock dummy `0xFF`)
- `ioctl()` → điều khiển CE / CSN GPIO

#### ioctl commands

| Command          | Value         | Tác dụng   |
|------------------|---------------|------------|
| NRF_IOC_CE_HIGH  | `_IO('n', 0)` | CE = HIGH  |
| NRF_IOC_CE_LOW   | `_IO('n', 1)` | CE = LOW   |
| NRF_IOC_CSN_HIGH | `_IO('n', 2)` | CSN = HIGH |
| NRF_IOC_CSN_LOW  | `_IO('n', 3)` | CSN = LOW  |

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

Hỗ trợ 2 platform qua macro `TEST_STM32`:

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

#define NRF_IOC_MAGIC    'n'
#define NRF_IOC_CE_HIGH  _IO(NRF_IOC_MAGIC, 0)
#define NRF_IOC_CE_LOW   _IO(NRF_IOC_MAGIC, 1)
#define NRF_IOC_CSN_HIGH _IO(NRF_IOC_MAGIC, 2)
#define NRF_IOC_CSN_LOW  _IO(NRF_IOC_MAGIC, 3)

extern int nrf_fd;

#define NRF_CE_HIGH()   ioctl(nrf_fd, NRF_IOC_CE_HIGH,  0)
#define NRF_CE_LOW()    ioctl(nrf_fd, NRF_IOC_CE_LOW,   0)
#define NRF_CSN_HIGH()  ioctl(nrf_fd, NRF_IOC_CSN_HIGH, 0)
#define NRF_CSN_LOW()   ioctl(nrf_fd, NRF_IOC_CSN_LOW,  0)

#endif /* TEST_STM32 */

void    SPI_Open(void);
uint8_t SPI_Transfer(uint8_t data);

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

#else
/* ── BeagleBone Black ───────────────────────────────────── */
#include <stdint.h>

int nrf_fd = -1;

void SPI_Open(void)
{
    nrf_fd = open("/dev/nrf24", O_RDWR);
    NRF_CE_LOW();
    NRF_CSN_HIGH();
}

uint8_t SPI_Transfer(uint8_t data)
{
    uint8_t rx = 0;
    if (write(nrf_fd, &data, 1) < 0) return 0;
    if (read(nrf_fd,  &rx,   1) < 0) return 0;
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

//#define TX_MODE


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


void NRF_WriteCmd(uint8_t cmd,const uint8_t *value, uint8_t len);
void NRF_ReadCmd(uint8_t cmd, uint8_t *value, uint8_t len);

void NRF_WriteReg_WithOneBit(uint8_t reg, uint8_t bit, uint8_t value);
uint8_t NRF_ReadReg_WithOneBit(uint8_t reg, uint8_t bit);

void NRF_WriteReg_WithOneByte(uint8_t reg, uint8_t value);
uint8_t NRF_ReadReg_WithOneByte(uint8_t reg);

void NRF_WriteReg_WithMultiBytes(uint8_t reg,const uint8_t *data, uint8_t len);
void NRF_ReadReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len);


#ifdef TX_MODE
void NRF_TX_Mode_Init(const uint8_t *addr, const uint8_t channel);
void NRF_SendData(uint8_t *data, uint8_t len);
void NRF_Flush_TX(void);

#else
void NRF_RX_Mode_Init(const uint8_t *addr, const uint8_t channel);
void NRF_StartListening(void);
void NRF_StopListening(void);
uint8_t NRF_DataReady(void);
void NRF_ReadData(uint8_t *data, uint8_t len);
void NRF_Flush_RX(void);
#endif

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

/**
 * Gửi một lệnh (cmd) kèm dữ liệu tới NRF qua SPI.
 * - Trình tự: CSN=0 -> gửi cmd -> gửi len byte dữ liệu (nếu có) -> CSN=1
 *
 * Tham số:
 * - cmd  : mã lệnh NRF (ví dụ NRF_CMD_W_REGISTER | reg)
 * - value: con trỏ dữ liệu kèm theo
 * - len  : số byte dữ liệu kèm theo
 *
 * Lưu ý: Nhánh len==1 sẽ thực hiện lấy giá trị của value 
 * chứ không phải lấy giá trị tại địa chỉ chứa trong value.
 */
void NRF_WriteCmd(uint8_t cmd,const uint8_t *value, uint8_t len)
{
	// Kéo CSN xuống thấp để bắt đầu phiên SPI với NRF24L01
	NRF_CSN_LOW();
	// Gửi byte lệnh đầu tiên
	SPI_Transfer(cmd);
	if(len == 1)
	{
		// Trường hợp chỉ có 1 byte dữ liệu đính kèm lệnh
		// Lưu ý: gọi SPI_Transfer(value) sẽ truyền giá trị con trỏ, không phải byte dữ liệu
		SPI_Transfer(*value); 
	}
	else 
	{
		// Gửi lần lượt từng byte, sau mỗi lần gửi con trỏ tăng 1
		while(len--) SPI_Transfer(*value++); 
	}
	// Kéo CSN lên cao để kết thúc phiên SPI
	NRF_CSN_HIGH();
}

/**
 * Đọc dữ liệu từ NRF bằng cách gửi cmd đọc rồi clock ra các byte bằng NOP.
 *
 * Tham số:
 * - cmd  : lệnh đọc (ví dụ NRF_CMD_R_REGISTER | reg)
 * - value: buffer đích để lưu dữ liệu đọc về
 * - len  : số byte cần đọc
 */
void NRF_ReadCmd(uint8_t cmd, uint8_t *value, uint8_t len)
{
	// Bắt đầu giao tiếp SPI
	NRF_CSN_LOW();
	// Gửi lệnh đọc trước
	SPI_Transfer(cmd);
	// Mỗi byte nhận về tương ứng 1 lần clock ra một byte NOP
	while(len--) *value++ = SPI_Transfer(NRF_CMD_NOP); 
	// Kết thúc giao tiếp SPI
	NRF_CSN_HIGH();
}

/** Ghi một thanh ghi 8-bit. */
void NRF_WriteReg_WithOneByte(uint8_t reg, uint8_t value) 
{
	// Ghi 1 byte vào thanh ghi: cmd = W_REGISTER | reg, dữ liệu là 1 byte
	NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, &value, 1);
}

/** Đọc một thanh ghi 8-bit và trả về giá trị. */
uint8_t NRF_ReadReg_WithOneByte(uint8_t reg) 
{
	uint8_t data;
	// Đọc 1 byte từ thanh ghi: cmd = R_REGISTER | reg
	NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, &data, 1);
	// Trả về giá trị đọc được
	return data;
}

/**
 * Set/Clear một bit trong thanh ghi 8-bit.
 * - value=1 -> set bit; value=0 -> clear bit.
 */
void NRF_WriteReg_WithOneBit(uint8_t reg, uint8_t bit, uint8_t value)
{
	// 1) Đọc giá trị hiện tại của thanh ghi
	uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);

	// 2) Set/Clear bit theo tham số value
	reg_value = value ? (reg_value | bit) : (reg_value & ~bit);

	// 3) Ghi trả lại thanh ghi với giá trị mới
	NRF_WriteReg_WithOneByte(reg, reg_value);
}

/** Kiểm tra một bit trong thanh ghi 8-bit, trả về 1 nếu đang set, ngược lại 0. */
uint8_t NRF_ReadReg_WithOneBit(uint8_t reg, uint8_t bit)
{
	uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);
	// Trả về 1 nếu bit được set, ngược lại 0
	return (reg_value & bit) && 1;
}

/** Ghi nhiều byte vào một thanh ghi (ví dụ địa chỉ TX/RX). */
void NRF_WriteReg_WithMultiBytes(uint8_t reg,const uint8_t *data, uint8_t len) 
{
	// Ghi nhiều byte liền kề (ví dụ địa chỉ 3-5 byte)
	NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, data, len);
}

/** Đọc nhiều byte từ một thanh ghi (ví dụ địa chỉ TX/RX). */
void NRF_ReadReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len) 
{
	// Đọc nhiều byte liên tiếp từ một thanh ghi bắt đầu tại reg
	NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, data, len);
}


/** Flush FIFO RX (xóa dữ liệu hàng đợi nhận). */
void NRF_Flush_RX(void)
{
	// Xóa toàn bộ FIFO nhận để tránh đọc dữ liệu cũ/lỗi
	NRF_CSN_LOW();
	SPI_Transfer(NRF_CMD_FLUSH_RX);
	NRF_CSN_HIGH();
}

/** Flush FIFO TX (xóa dữ liệu hàng đợi phát). */
void NRF_Flush_TX(void)
{
	// Xóa toàn bộ FIFO phát để chuẩn bị nạp payload mới
	NRF_CSN_LOW();
	SPI_Transfer(NRF_CMD_FLUSH_TX);
	NRF_CSN_HIGH();
}

/** Đọc thanh ghi STATUS bằng cách clock lệnh NOP; trả về byte STATUS. */
uint8_t NRF_ReadStatus(void)
{
	uint8_t status;
	// Đọc STATUS bằng cách clock 1 byte NOP khi CSN thấp
	NRF_CSN_LOW();
	status = SPI_Transfer(NRF_CMD_NOP); // đọc STATUS
	NRF_CSN_HIGH();
	return status;
}

/*------------------------------------------ TX Mode ------------------------------------------*/
#ifdef TX_MODE

/**
 * Khởi tạo NRF ở chế độ TX (phát): bật CRC, Auto-ACK, cấu hình địa chỉ/kênh,
 * set payload width, clear cờ STATUS, flush TX, về PWR_UP và CE thấp.
 *
 * Tham số:
 * - addr   : địa chỉ 5 byte cho TX_ADDR và RX_ADDR_P0 (dùng Auto-ACK)
 * - channel: kênh RF (0..127)
 */
void NRF_TX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
	SPI_Open(); // Mở SPI trước khi cấu hình NRF
	delay_ms(20);
	
	// Bật CRC (1 hoặc 2 byte tùy CONFIG), PRIM_RX=0 (TX mode)
	NRF_WriteReg_WithOneByte(NRF_REG_CONFIG, 				CONFIG_EN_CRC); // CONFIG: EN_CRC = 1
	// Bật Auto-ACK cho pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_AA, 				ENAA_P0); // EN_AA: auto-ack
	// Bật địa chỉ pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR, 		ERX_P0); // EN_RXADDR: enable pipe0
	// Thiết lập độ dài địa chỉ (5 byte): theo datasheet lưu dưới dạng (width-2)
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW, 			(ADDRESS_LENGTH - 0x02)); // SETUP_AW: 5 bytes addr
	// Thiết lập tự động retry: delay ~1000us, tối đa 15 lần
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR, 		0x3f); // SETUP_RETR: 1000us, 15 retries
	// Chọn kênh RF (0..127)
	NRF_WriteReg_WithOneByte(NRF_REG_RF_CH, 				channel & 0x7F);   // RF_CH: channel
	
	// Ghi địa chỉ phát (TX_ADDR)
	NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR, addr, ADDRESS_LENGTH); 
	
	// RX_ADDR_P0 phải trùng với địa chỉ TX để Auto-ACK hoạt động
	NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH); 
	
	// Cấu hình kích thước payload cố định cho pipe0
	NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0, PACKET_SIZE);  
	
	// Clear cờ trong STATUS
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 		STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);
	
	NRF_Flush_TX();
	
	// TX mode: CE = 0, chỉ pulse khi gửi
	NRF_CE_LOW();

	// Bật nguồn RF (PWR_UP) và đợi mạch ổn định
	NRF_WriteReg_WithOneBit(NRF_REG_CONFIG, CONFIG_PWR_UP, 1); // CONFIG: PWR_UP=1
	delay_ms(10);
}

/**
 * Gửi một gói dữ liệu ở TX mode.
 * - Clamp độ dài về PACKET_SIZE, ghi payload vào FIFO TX (W_TX_PAYLOAD),
 *   pulse CE >= ~10us để phát, chờ TX_DS hoặc MAX_RT rồi xóa cờ STATUS.
 */
void NRF_SendData(uint8_t *data, uint8_t len) 
{
	uint8_t temp[PACKET_SIZE];
	// Đảm bảo CE thấp trước khi nạp payload TX
	NRF_CE_LOW();

	// Xóa FIFO TX để tránh chèn payload vào hàng đợi cũ
	NRF_Flush_TX();

	// Giới hạn độ dài về PACKET_SIZE
	len = len > PACKET_SIZE ? PACKET_SIZE : len;
	// Điền 0 phần còn lại để đủ kích thước cố định
	memset(temp, 0, PACKET_SIZE);
	// Sao chép dữ liệu người dùng vào buffer tạm
	memmove(temp, data, len);
	// Nạp payload vào FIFO TX
	NRF_WriteCmd(NRF_CMD_W_TX_PAYLOAD, temp, PACKET_SIZE);

	// Pulse CE để phát
	NRF_CE_HIGH();
	// Giữ CE mức cao tối thiểu ~10us để bắt đầu truyền
	delay_us(50); // delay ngắn ~10us
	NRF_CE_LOW();

	// Đợi TX_DS hoặc MAX_RT
	while (!(NRF_ReadStatus() & (STATUS_TX_DS | STATUS_MAX_RT)));

	// Xóa cờ
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 0x70);
}

/*------------------------------------------ RX Mode ------------------------------------------*/
#else

/**
 * Khởi tạo NRF ở chế độ RX (nghe/nhận): bật CRC, PRIM_RX, Auto-ACK, địa chỉ,
 * kênh, RF_SETUP, payload width; clear cờ STATUS, bật PWR_UP, flush RX, CE=1.
 *
 * Tham số:
 * - addr   : địa chỉ 5 byte cho RX_ADDR_P0 (và TX_ADDR để ACK trả về)
 * - channel: kênh RF (0..127)
 */
void NRF_RX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
	SPI_Open(); // Mở SPI trước khi cấu hình NRF
	delay_ms(20);
	
	// Bật CRC, chuyển về chế độ nhận (PRIM_RX)
	NRF_WriteReg_WithOneByte(NRF_REG_CONFIG,				CONFIG_EN_CRC | CONFIG_PRIM_RX); // CONFIG: EN_CRC = 1, PWR_UP=1, PRIM_RX=1
	// Bật Auto-ACK cho pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_AA,					ENAA_P0); // EN_AA: auto-ack
	// Bật địa chỉ pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR,			ERX_P0); // EN_RXADDR: enable pipe0
	// Độ dài địa chỉ = 5 byte
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW,			(ADDRESS_LENGTH - 0x02)); // SETUP_AW: 5 bytes addr
	// Retry dùng cho chế độ TX (cũng thiết lập trước)
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR,		0x3f); // SETUP_RETR: 1000us, 15 retries
	// Kênh RF
	NRF_WriteReg_WithOneByte(NRF_REG_RF_CH,					channel & 0x7F);   // RF_CH: channel
	// Tốc độ 1Mbps, công suất 0dBm (0x0f phụ thuộc cấu hình DATASHEET)
	NRF_WriteReg_WithOneByte(NRF_REG_RF_SETUP,			0x0f); // RF_SETUP: 1Mbps, 0dBm
	
	// Thiết lập TX_ADDR (dùng cho trả ACK) và RX_ADDR_P0 trùng nhau
	NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR, addr, ADDRESS_LENGTH); 
	
	NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH); 
	
	// Set payload width cho pipe0 (32 byte)
	NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0, PACKET_SIZE); 
	
	// Clear interrupt flags trong STATUS
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS,  	(STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT));
	
	// Bật nguồn RF
	NRF_WriteReg_WithOneBit(NRF_REG_CONFIG,	CONFIG_PWR_UP, 1);
	delay_ms(10);
	
	// Đảm bảo FIFO RX sạch trước khi bắt đầu lắng nghe
	NRF_Flush_RX();

	NRF_StopListening();
}

/** Bắt đầu lắng nghe (CE = 1 ở RX mode). */
void NRF_StartListening(void)
{
	// Kéo CE lên cao để bắt đầu lắng nghe (PRX)
	NRF_CE_HIGH();
}

/** Dừng lắng nghe (CE = 0). */
void NRF_StopListening(void)
{
	// Kéo CE xuống thấp để dừng lắng nghe
	NRF_CE_LOW();
}


/** Trả về khác 0 nếu có dữ liệu sẵn (cờ RX_DR set). */
uint8_t NRF_DataReady(void) 
{
	uint8_t status = NRF_ReadStatus();
	// Kiểm tra cờ RX_DR; lưu ý không cho biết pipe nào, muốn biết pipe cần đọc RX_P_NO
	return status & STATUS_RX_DR;
}

/** Đọc payload từ FIFO RX (R_RX_PAYLOAD). */
void NRF_Read_RX_Payload(uint8_t *data, uint8_t len)
{
	// Đọc ra len byte từ đầu FIFO RX; nếu len < kích thước payload, phần còn lại vẫn nằm trong FIFO
	NRF_ReadCmd(NRF_CMD_R_RX_PAYLOAD, data, len);
}

/**
 * Đọc dữ liệu cấp cao:
 * - Luôn đọc PACKET_SIZE byte vào buffer tạm
 * - Sao chép tối đa len byte ra buffer người dùng
 * - Xóa cờ RX_DR sau khi đọc
 */
void NRF_ReadData(uint8_t *data, uint8_t len) 
{
	uint8_t temp[PACKET_SIZE];
	// Luôn đọc đủ PACKET_SIZE từ FIFO RX vào buffer tạm
	NRF_Read_RX_Payload(temp, PACKET_SIZE);

	// Chỉ copy tối đa len byte cho người gọi
	len = len > PACKET_SIZE ? PACKET_SIZE : len;
	memmove(data, temp, len);

	// Xóa cờ RX_DR
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, STATUS_RX_DR);
}

#endif
```


> `TEST_STM32` không được define khi build với Buildroot → tự động dùng code BBB.
> Để build cho STM32: thêm `-DTEST_STM32` vào CFLAGS.

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
make
```
Kiểm tra staging:
```bash
ls output/staging/usr/lib/libnrf24*
ls output/staging/usr/include/nrf*
```

Kết quả đúng:
```
output/staging/usr/lib/libnrf24.a
output/staging/usr/lib/libnrf24.so
output/staging/usr/include/nrf24l01.h
output/staging/usr/include/nrf_spi.h
```
