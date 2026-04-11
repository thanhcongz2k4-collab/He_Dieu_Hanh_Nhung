# Button Driver & Test

Linux kernel module cho GPIO button trên BeagleBone Black (P8_11 / GPIO 525),
kèm userspace test application. Build qua Buildroot.

---

## Cấu trúc thư mục

```
package/
├── button/
│   ├── src/
│   │   ├── btn.c          # Kernel module
│   │   └── Makefile       # Build standalone (ngoài Buildroot)
│   ├── button.mk          # Buildroot package
│   └── Config.in
│
└── button_test/
    ├── src/
    │   └── button_test.c  # Userspace test app
    ├── button_test.mk     # Buildroot package
    └── Config.in
```

---

## Hoạt động

Driver tạo char device `/dev/btn`. Khi userspace gọi `read()`, process sẽ **block** cho đến khi có ngắt GPIO (nhấn hoặc nhả nút). Driver trả về `"1\n"` khi nhấn, `"0\n"` khi nhả.

```
User process          Kernel driver           Hardware
    |                     |                      |
    |---- read() -------->|                      |
    |                  [block]                   |
    |                     |<--- GPIO interrupt --|
    |                  [wake up]                 |
    |<--- "1\n" ----------|                      |
```

Debounce 50ms được xử lý trong ISR bằng `jiffies`.

---

## Source code

### `package/button/src/btn.c`

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/wait.h>

#define DEVICE_NAME "btn"
#define CLASS_NAME  "btn"
#define BUTTON_GPIO 525
#define DEBOUNCE_MS 50

static int major;
static struct class *btn_class;
static struct device *btn_device;
static int irq_num;
static int button_state;
static unsigned long last_jiffies;

static DECLARE_WAIT_QUEUE_HEAD(btn_waitq);
static int event_flag = 0;

/* -------------------------------------------------- */
/* ISR                                                */
/* -------------------------------------------------- */
static irqreturn_t button_isr(int irq, void *dev_id)
{
    unsigned long now = jiffies;

    if (time_before(now, last_jiffies + msecs_to_jiffies(DEBOUNCE_MS)))
        return IRQ_HANDLED;

    last_jiffies = now;
    button_state = !gpio_get_value(BUTTON_GPIO); /* active low */
    printk(KERN_INFO "BTN: %d\n", button_state);

    event_flag = 1;
    wake_up_interruptible(&btn_waitq);
    return IRQ_HANDLED;
}

/* -------------------------------------------------- */
/* READ — block cho đến khi có interrupt              */
/* -------------------------------------------------- */
static ssize_t btn_read(struct file *f, char __user *buf,
                        size_t len, loff_t *off)
{
    char tmp[4];
    size_t slen;
    int ret;

    ret = wait_event_interruptible(btn_waitq, event_flag);
    if (ret)
        return -ERESTARTSYS;

    event_flag = 0;

    snprintf(tmp, sizeof(tmp), "%d\n", button_state);
    slen = strlen(tmp);
    if (slen > len)
        slen = len;

    if (copy_to_user(buf, tmp, slen))
        return -EFAULT;

    return slen;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = btn_read,
};

/* -------------------------------------------------- */
/* INIT                                               */
/* -------------------------------------------------- */
static int __init btn_init(void)
{
    printk(KERN_INFO "BTN init\n");

    if (gpio_request(BUTTON_GPIO, "btn_gpio")) {
        printk(KERN_ERR "gpio_request failed\n");
        return -EBUSY;
    }

    gpio_direction_input(BUTTON_GPIO);

    irq_num = gpio_to_irq(BUTTON_GPIO);
    if (irq_num < 0) {
        printk(KERN_ERR "gpio_to_irq failed\n");
        gpio_free(BUTTON_GPIO);
        return -EINVAL;
    }

    if (request_irq(irq_num, button_isr,
                    IRQF_TRIGGER_FALLING,
                    DEVICE_NAME, NULL)) {
        printk(KERN_ERR "request_irq failed\n");
        gpio_free(BUTTON_GPIO);
        return -EBUSY;
    }

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        printk(KERN_ERR "register_chrdev failed\n");
        free_irq(irq_num, NULL);
        gpio_free(BUTTON_GPIO);
        return major;
    }

    btn_class = class_create(CLASS_NAME);
    if (IS_ERR(btn_class)) {
        printk(KERN_ERR "class_create failed\n");
        unregister_chrdev(major, DEVICE_NAME);
        free_irq(irq_num, NULL);
        gpio_free(BUTTON_GPIO);
        return PTR_ERR(btn_class);
    }

    btn_device = device_create(btn_class, NULL,
                               MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(btn_device)) {
        printk(KERN_ERR "device_create failed\n");
        class_destroy(btn_class);
        unregister_chrdev(major, DEVICE_NAME);
        free_irq(irq_num, NULL);
        gpio_free(BUTTON_GPIO);
        return PTR_ERR(btn_device);
    }

    printk(KERN_INFO "BTN loaded: /dev/btn (GPIO=%d IRQ=%d)\n",
           BUTTON_GPIO, irq_num);
    return 0;
}

/* -------------------------------------------------- */
/* EXIT                                               */
/* -------------------------------------------------- */
static void __exit btn_exit(void)
{
    device_destroy(btn_class, MKDEV(major, 0));
    class_destroy(btn_class);
    unregister_chrdev(major, DEVICE_NAME);
    free_irq(irq_num, NULL);
    gpio_free(BUTTON_GPIO);
    printk(KERN_INFO "BTN unloaded\n");
}

module_init(btn_init);
module_exit(btn_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rimuru");
MODULE_DESCRIPTION("Button driver using linux/gpio.h");
```

---

### `package/button/src/Makefile`

```makefile
obj-m += btn.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

---

### `package/button/button.mk`

```makefile
BUTTON_VERSION = 1.0
BUTTON_SITE = $(TOPDIR)/package/button/src
BUTTON_SITE_METHOD = local

define BUTTON_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE=$(TARGET_CROSS) \
		modules
endef

define BUTTON_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/btn.ko \
		$(TARGET_DIR)/lib/modules/btn.ko
endef

$(eval $(generic-package))
```

---

### `package/button/Config.in`

```kconfig
config BR2_PACKAGE_BUTTON
	bool "button"
	depends on BR2_LINUX_KERNEL
	help
	  GPIO button kernel module for /dev/btn (GPIO 525 / P8_11)
```

---

### `package/button_test/src/button_test.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#define DEVICE "/dev/btn"

static int fd;

void sig_handler(int sig)
{
    close(fd);
    printf("\nExiting.\n");
    exit(0);
}

int main()
{
    char buf[16];

    signal(SIGINT, sig_handler);

    fd = open(DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    printf("Waiting for button events...\n");

    while (1) {
        int n;

        memset(buf, 0, sizeof(buf));

        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            break;
        }

        buf[n] = '\0';

        if (buf[0] == '1')
            printf("PRESSED\n");
        else
            printf("RELEASED\n");
    }

    close(fd);
    return 0;
}
```

---

### `package/button_test/button_test.mk`

```makefile
BUTTON_TEST_VERSION = 1.0
BUTTON_TEST_SITE = package/button_test/src
BUTTON_TEST_SITE_METHOD = local

define BUTTON_TEST_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		$(@D)/button_test.c -o $(@D)/button_test
endef

define BUTTON_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/button_test \
		$(TARGET_DIR)/usr/bin/button_test
endef

$(eval $(generic-package))
```

---

### `package/button_test/Config.in`

```kconfig
config BR2_PACKAGE_BUTTON_TEST
	bool "button_test"
	help
	  Simple user-space test for /dev/btn
```

---

## Build với Buildroot

### 1. Thêm package vào Buildroot tree

```bash
cp -r package/button      <buildroot>/package/button
cp -r package/button_test <buildroot>/package/button_test
```

Thêm vào `<buildroot>/package/Config.in`:

```kconfig
source "package/button/Config.in"
source "package/button_test/Config.in"
```

### 2. Enable trong menuconfig

```bash
make menuconfig
```

Tìm và bật:

```
Target packages
    └── button
    └── button_test
```

### 3. Build

```bash
make button
make button_test
```

Sau build:
- Kernel module được cài tại: `/lib/modules/btn.ko`
- Test app được cài tại: `/usr/bin/button_test`

---

## Build standalone (ngoài Buildroot)

```bash
cd package/button/src
make KDIR=/path/to/kernel/source
```

---

## Sử dụng trên target

### Load driver

```bash
insmod /lib/modules/btn.ko
```

Kiểm tra:

```bash
dmesg | tail
# BTN loaded: /dev/btn (GPIO=525 IRQ=...)

ls /dev/btn
```

### Chạy test

```bash
button_test
# Waiting for button events...
# PRESSED
# RELEASED
```

Mỗi lần nhấn/nhả nút in ra một dòng. Ctrl+C để thoát sạch.

### Unload driver

```bash
rmmod btn
```

---

## Cấu hình

| Tham số | Giá trị | Mô tả |
|---|---|---|
| `BUTTON_GPIO` | 525 | GPIO number (P8_11) |
| `DEBOUNCE_MS` | 50 | Thời gian debounce (ms) |

Sửa trong `btn.c` rồi build lại nếu muốn thay đổi.

---

## Yêu cầu

- BeagleBone Black, kernel 4.x trở lên
- Buildroot với Linux kernel headers tương ứng
- Nút nhấn nối giữa P8_11 và GND (active low)
