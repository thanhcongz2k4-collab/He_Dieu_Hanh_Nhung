# BeagleBone Black — Custom Character Driver: Rimuru Driver (Group 2)

Dự án này thực hiện xây dựng một **Linux Kernel Module (Character Device Driver)** chạy trên nền tảng **BeagleBone Black (AM335x)**. Hệ điều hành được tùy chỉnh bằng công cụ **Buildroot**.

---

## 1. Mục tiêu bài thực hành

- Xây dựng cấu trúc Driver hoàn chỉnh (Init, Exit, Open, Release, Read, Write)
- Cơ chế tự động cấp phát Major Number và tạo Device Node trong `/dev/`
- Giao tiếp dữ liệu giữa User Space và Kernel Space thông qua `copy_to_user` và `copy_from_user`
- Kiểm thử tính năng trên BeagleBone Black

---

## 2. Cấu trúc Package trong Buildroot

Package được tổ chức theo quy tắc của Buildroot để tự động hóa việc biên dịch:

```
package/rimuru_driver/
├── Config.in               # Cấu hình Menuconfig
├── rimuru_driver.mk        # Chỉ thị biên dịch của Buildroot
└── src/
    ├── Makefile            # Kbuild cho Kernel Module
    └── rimuru_driver.c     # Mã nguồn C của Driver
```

---

## 3. Nội dung kỹ thuật

### 3.1. Kernel Driver (rimuru_driver.c)

Driver thực hiện đăng ký một thiết bị ký tự. Khi User ghi dữ liệu, Driver lưu vào một vùng đệm (`rimuru_buffer`) và sẵn sàng nhả lại dữ liệu khi User đọc.

**Các hàm quan trọng:**

| Hàm | Mô tả |
|-----|-------|
| `dev_open()` | Mở thiết bị |
| `dev_release()` | Đóng thiết bị |
| `dev_read()` | Đọc dữ liệu từ kernel → user space (sử dụng `copy_to_user`) |
| `dev_write()` | Ghi dữ liệu từ user space → kernel (sử dụng `copy_from_user`) |
| `rimuru_driver_init()` | Hàm khởi tạo - đăng ký character device |
| `rimuru_driver_exit()` | Hàm thoát - hủy đăng ký character device |

**Mã nguồn (rimuru_driver.c):**

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>          // Thư viện cho Character Device (register_chrdev)
#include <linux/device.h>      // Thư viện cho class_create, device_create
#include <linux/uaccess.h>     // Thư viện cho copy_to_user và copy_from_user

#define DEVICE_NAME "rimuru_dev"
#define CLASS_NAME  "rimuru_class"

#define SIZE    1024

static int major_number;
static struct class* char_class  = NULL; 
static struct device* char_device = NULL;
static char rimuru_buffer[SIZE]; 


static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Rimuru đã mở Device thành công \n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Rimuru đã đóng Device \n");
    return 0;
}


static ssize_t dev_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *ppos) {
    if (count > SIZE - 1) count = SIZE - 1; 

    if (copy_from_user(rimuru_buffer, user_buffer, count)) {
        return -EFAULT;
    }

    rimuru_buffer[count] = '\0';
    printk(KERN_INFO "Rimuru nhận được nội dung: %s\n", rimuru_buffer);
    
    return count;
}


static ssize_t dev_read(struct file *file, char __user *user_buffer, size_t count, loff_t *ppos) {
    if (*ppos > 0) return 0;
    if (count > strlen(rimuru_buffer)) count = strlen(rimuru_buffer);

    if (copy_to_user(user_buffer, rimuru_buffer, count)) {
        return -EFAULT;
    }

    printk(KERN_INFO "Rimuru: Đang gửi dữ liệu [%s] về cho User Space\n", rimuru_buffer);

    *ppos += count;
    return count;
}


static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};


static int __init rimuru_driver_init(void) {
    // cấp phát major 
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "Rimuru Không thể đăng ký Major number\n");
        return major_number;
    }
    printk(KERN_INFO "Rimuru đã cấp Major number: %d\n", major_number);
    // Tạo class
    char_class = class_create(CLASS_NAME);
    if (IS_ERR(char_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(char_class);
    }

    // Tạo device
    char_device = device_create(char_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(char_device)) {
        class_destroy(char_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(char_device);
    }
    printk(KERN_INFO "Rimuru Driver: Hello từ Slime Tempest!\n");
    return 0;
}
static void __exit rimuru_driver_exit(void) {
    device_destroy(char_class, MKDEV(major_number, 0)); 
    class_destroy(char_class);                        
    unregister_chrdev(major_number, DEVICE_NAME);     
    printk(KERN_INFO "Rimuru Driver: Tạm biệt, Module đã được gỡ\n");
}

module_init(rimuru_driver_init);
module_exit(rimuru_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rimuru - Nhom 2");

```

### 3.2. Cấu hình Buildroot (rimuru_driver.mk)

Sử dụng hạ tầng kernel-module để liên kết với mã nguồn Linux Kernel hiện tại.

```makefile
RIMURU_DRIVER_VERSION = 1.0
RIMURU_DRIVER_SITE = $(TOPDIR)/package/rimuru_driver/src
RIMURU_DRIVER_SITE_METHOD = local

#Kernel Module
$(eval $(kernel-module))
#package Module
$(eval $(generic-package))
```

### 3.3. Cấu hình Menuconfig (Config.in)

```
config BR2_PACKAGE_RIMURU_DRIVER
	bool "rimuru_driver"
	depends on BR2_LINUX_KERNEL
	help
	  Linux Kernel Driver cho phep doc/ghi du lieu 
	  giua User Space va Kernel Space.
	  Project cua Rimuru - Nhom 2.

```

### 3.4. Makefile cho Kernel Module

```makefile
obj-m += rimuru_driver.o

all:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) clean

```

---

## 4. Quy trình triển khai

### Bước 1: Chuẩn bị Buildroot

1. Copy package vào `buildroot/package/Config.in`
2. Cấu hình package trong Menuconfig:

```bash
cd /path/to/buildroot
make menuconfig
# Chọn: Target packages → Miscellaneous → rimuru_driver
```

### Bước 2: Biên dịch

Tại thư mục gốc của Buildroot:

```bash
make rimuru_driver-rebuild
make
```

### Bước 3: Nạp và kiểm tra trên BeagleBone Black

Sau khi boot hệ thống, thực hiện các lệnh sau:

#### 3.1. Nạp Module

```bash
modprobe rimuru_driver
```

Kiểm tra dmesg/log hệ thống sẽ báo Major number được cấp (ví dụ: 248):

```bash
dmesg | tail -5
```

#### 3.2. Kiểm tra Device Node

```bash
ls -l /dev/rimuru_dev
```

Nếu chưa tồn tại, tạo thủ công:

```bash
mknod /dev/rimuru_dev c <MAJOR> 0
```

#### 3.3. Giao tiếp Write (User → Kernel)

Ghi dữ liệu vào driver:

```bash
echo "Rimuru Tempest Nhom 2" > /dev/rimuru_dev
```

Kiểm tra log:

```bash
dmesg | grep rimuru
```

#### 3.4. Giao tiếp Read (Kernel → User)

Đọc dữ liệu từ driver:

```bash
cat /dev/rimuru_dev
```

---

## 5. Kết quả đạt được (Demo)

Dưới đây là kết quả thực tế thu được từ Terminal:

![open_release_write_read](anh.jpg)

---

## 6. LED GPIO Kernel Driver - Phần mở rộng

Dự án xây dựng hệ thống điều khiển LED trên BeagleBone Black bằng cách tác động trực tiếp vào thanh ghi phần cứng (ioremap) và đóng gói thành các Package chuyên nghiệp trong Buildroot.

### 6.1. Cấu trúc cây thư mục

```
buildroot/
└── package/
    ├── led_driver/              
    │   ├── Config.in               
    │   ├── led_driver.mk         
    │   └── src/
    │       ├── led_driver.c      
    │       └── Makefile             
    └── led_blink_app/             
        ├── Config.in               
        ├── led_blink_app.mk        
        └── src/
            └── led_blink_app.c     

```

### 6.2. Bài 1: Kernel Driver GPIO LED (ioremap)

Sử dụng phương pháp ánh xạ bộ nhớ để điều khiển LED tại chân P8_11 (GPIO1_13) dựa trên tài liệu kỹ thuật của chip AM335x.

#### Mã nguồn Driver (led_driver.c)

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/io.h>    

#define DEVICE_NAME "rimuru_led"  
#define CLASS_NAME  "rimuru_class" 

/* Thông số phần cứng AM335x cho GPIO1 */
#define GPIO1_BASE          0x4804C000
#define GPIO1_SIZE          0x1000
#define GPIO_OE             0x134      // Thanh ghi cấu hình In/Out
#define GPIO_SETDATAOUT     0x194      // Thanh ghi Bật LED
#define GPIO_CLEARDATAOUT   0x190      // Thanh ghi Tắt LED
#define LED_PIN             (1 << 13)  // Chân P8_11 là GPIO1_13

static int major_number;
static struct class* led_class  = NULL;
static struct device* led_device = NULL;
void __iomem *base_addr; 

static int dev_open(struct inode *inodep, struct file *filep) {
    return 0;
}

static ssize_t dev_write(struct file *file, const char __user *user_buffer,
                         size_t count, loff_t *ppos) {
    char val;
    if (copy_from_user(&val, user_buffer, 1)) return -EFAULT;

    if (val == '1') {
        
        iowrite32(LED_PIN, base_addr + GPIO_SETDATAOUT);
        printk(KERN_INFO "Rimuru LED: ON \n");
    } else if (val == '0') {
       
        iowrite32(LED_PIN, base_addr + GPIO_CLEARDATAOUT);
        printk(KERN_INFO "Rimuru LED: OFF \n");
    }
    return count;
}


static ssize_t dev_read(struct file *file, char __user *user_buffer, size_t count, loff_t *ppos) {
    if (*ppos > 0) return 0;
    
    uint32_t reg_val = ioread32(base_addr + GPIO_SETDATAOUT);
    char status = (reg_val & LED_PIN) ? '1' : '0';
    
    if (copy_to_user(user_buffer, &status, 1)) return -EFAULT;
    *ppos += 1;
    return 1;
}

static struct file_operations fops = { 
    .open = dev_open,
    .write = dev_write,
    .read = dev_read, 
};

static int __init led_driver_init(void) {
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    led_class  = class_create(CLASS_NAME);
    led_device = device_create(led_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);

    // bao map ofset từ 000-FFF
    base_addr = ioremap(GPIO1_BASE, GPIO1_SIZE);
    if (!base_addr) return -ENOMEM;

    // di chuyển tới thanh ghi OE và lấy trạng thái thanh ghi hiện tại
    uint32_t oe_val = ioread32(base_addr + GPIO_OE);
    oe_val &= ~LED_PIN; // set GPIO1_13 sang chế độ output
    iowrite32(oe_val, base_addr + GPIO_OE); // ghi lại lên thanh ghi

    printk(KERN_INFO "Rimuru LED Driver (ioremap) Loaded\n");
    return 0;
}

static void __exit led_driver_exit(void) {
    iounmap(base_addr);
    device_destroy(led_class, MKDEV(major_number, 0));
    class_destroy(led_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "Rimuru LED Driver Unloaded\n");
}

module_init(led_driver_init);
module_exit(led_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rimuru - Nhom 2");

```

#### Makefile cho Driver (src/Makefile)

```makefile
obj-m += led_driver.o

all:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) clean
```

#### Cấu hình Buildroot (led_driver.mk)

```makefile
LED_DRIVER_VERSION = 1.0
LED_DRIVER_SITE = $(TOPDIR)/package/led_driver/src
LED_DRIVER_SITE_METHOD = local
LED_DRIVER_LICENSE = GPL-v2

# Khai báo đây là một kernel module
$(eval $(kernel-module))
$(eval $(generic-package))
```
#### Các gói phụ thuộc (Config.in)
```
config BR2_PACKAGE_LED_DRIVER
	bool "led_driver"
	depends on BR2_LINUX_KERNEL
	help
	  Kernel module driver for Controlling BBB LEDs.

```
### 6.3. Bài 2: User-space Blink Application

Ứng dụng thực hiện giao tiếp với Device Node `/dev/rimuru_led` để tạo hiệu ứng nhấp nháy LED.

#### Mã nguồn App (led_blink_app.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/rimuru_led"

int main() {
    int fd = open(DEVICE_PATH, O_WRONLY);
    int delay_ms;

    if (fd < 0) {
        perror("Lỗi: Không tìm thấy Driver!");
        return -1;
    }
    printf("Nhập thời gian chớp tắt (ms): ");
    if (scanf("%d", &delay_ms) != 1) return -1;

    printf("Bắt đầu Blink LED... Nhấn Ctrl+C để dừng\n");
    while(1) {
        write(fd, "1", 1);
        usleep(delay_ms * 1000);
        write(fd, "0", 1);
        usleep(delay_ms * 1000);
    }

    close(fd);
    return 0;
}

```

#### Cấu hình Buildroot (led_blink_app.mk)

```makefile
LED_BLINK_APP_VERSION = 1.0
LED_BLINK_APP_SITE = $(TOPDIR)/package/led_blink_app/src
LED_BLINK_APP_SITE_METHOD = local

define LED_BLINK_APP_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define LED_BLINK_APP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blink_app $(TARGET_DIR)/usr/bin/blink_app
endef

$(eval $(generic-package))

```
#### Các gói phụ thuộc (Config.in)
```
config BR2_PACKAGE_LED_BLINK_APP
	bool "led_blink_app"
	help
	  Ứng dụng C để điều khiển Blink LED thông qua led_driver.

```
### 6.4. Các lệnh vận hành & Kiểm tra hệ thống

Sau khi nạp firmware và khởi động BeagleBone Black:

#### Bước 1: Nạp Module

```bash
insmod /lib/modules/6.16.5/updates/led_driver.ko
```

#### Bước 2: Kiểm tra tính đúng đắn của Driver

**Kiểm tra Major Number:**

```bash
cat /proc/devices | grep rimuru_led
# Kết quả mong đợi: 248 rimuru_led
```

**Kiểm tra Device Class (Sysfs):**

Xác nhận Kernel đã tạo Class thành công để quản lý thiết bị:

```bash
ls -l /sys/class/rimuru_class/
```

**Kiểm tra Device Node:**

Kiểm tra file thiết bị được tạo tự động trong `/dev`:

```bash
ls -l /dev/rimuru_led
# Kết quả mong đợi: crw------- 1 root root 248, 0 ...
```

#### Bước 3: Vận hành ứng dụng

**Chạy ứng dụng chớp tắt ngầm:**

```bash
blink_app
```

**Dừng ứng dụng:**

```bash
killall led_blink_app
```

### 6.5. Tài liệu tham khảo

- **[BeagleBone Black System Reference Manual](https://docs.beagleboard.org/beaglebone-black.pdf):** Tra cứu Pinout P8_11.
- **[AM335x Technical Reference Manual (TRM)](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf):**
  - Trang 172: Địa chỉ Base GPIO1 (0x4804C000).
  - Trang 4877: Các thanh ghi Offset OE, SETDATAOUT, CLEARDATAOUT.

### 6.6. Video Kết quả

- **[Video Demo Kết quả cuối cùng](https://drive.google.com/file/d/19NTK0dLThfQ8XZRmzLy-K3fCDZtdbcN-/view?usp=sharing)** - Hình ảnh hoạt động LED blinking trên BeagleBone Black thực tế.

