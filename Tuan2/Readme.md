# TUẦN 2 – BUILD U-BOOT & KERNEL LINUX CHO BEAGLEBONE BLACK - Nhóm 2

## 1. Mục tiêu

Sau khi hoàn thành nội dung tuần này, sinh viên sẽ:

- Hiểu vai trò của U-Boot trong chuỗi khởi động Embedded Linux
- Build thành công U-Boot cho BeagleBone Black 
- Chuẩn bị thẻ SD boot đúng chuẩn phần cứng BBB
- Build Linux Kernel cho BeagleBone Black
- Boot Kernel bằng U-Boot CMD và extlinux.conf
- Hiểu nguyên nhân Kernel Panic khi thiếu root filesystem

---

## 2. Chuẩn bị môi trường

### Phần cứng

- BeagleBone Black (AM335x)
- Thẻ nhớ SD ≥ 8GB
- Cáp USB–UART
- Máy tính chạy Ubuntu 20.04 / 22.04

### Phần mềm

- Toolchain ARM (cross-compile)
- Minicom hoặc Picocom
- Các công cụ build cần thiết

Cài đặt các gói hỗ trợ:

```bash
sudo apt update
sudo apt install build-essential git bc bison flex libssl-dev \
libncurses5-dev libncursesw5-dev device-tree-compiler \
u-boot-tools gparted
```

## 3. Build U-Boot cho BeagleBone Black

### Chuẩn bị thư mục làm việc

```bash
mkdir -p ~/bbb/u-boot
cd ~/bbb/u-boot
```

### Tải mã nguồn U-Boot

```bash
git clone https://source.denx.de/u-boot/u-boot.git
cd u-boot
git checkout v2024.04
```

### Thiết lập Toolchain

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
```

Lưu ý: Bắt buộc có dấu `-` ở cuối `CROSS_COMPILE`.

### Cấu hình và biên dịch U-Boot

```bash
make distclean
make am335x_evm_defconfig
make -j$(nproc)
```

Sau khi build thành công, trong thư mục U-Boot phải xuất hiện:

- `MLO` – First stage bootloader
- `u-boot.img` – Second stage bootloader

## 4. Chuẩn bị thẻ nhớ SD

### Kiểm tra tên thiết bị thẻ nhớ

```bash
lsblk
```

Ví dụ thẻ nhớ là `/dev/sdb`.

### Tạo bảng phân vùng (MBR / MS-DOS)

```bash
sudo fdisk /dev/sdb
```

Thao tác trong fdisk:

- `o` → tạo bảng phân vùng DOS
- `n` → tạo phân vùng BOOT (Primary, +128M)
- `n` → tạo phân vùng ROOTFS (Primary, phần còn lại)
- `t` → chọn phân vùng 1 → nhập `c` (W95 FAT32 LBA)
- `a` → chọn phân vùng 1 để bật boot flag
- `w` → ghi và thoát

Kiểm tra lại:

```bash
lsblk -f
```

Phân vùng `/dev/sdb1` phải có cờ boot (*).

### Định dạng phân vùng

```bash
sudo mkfs.vfat -F 32 /dev/sdb1
sudo mkfs.ext4 /dev/sdb2
```

### Copy file U-Boot vào thẻ nhớ

Rút thẻ nhớ và cắm lại để hệ thống tự mount, sau đó copy:

```bash
cp MLO /media/$USER/BOOT/
sync
cp u-boot.img /media/$USER/BOOT/
sync
```

Thứ tự copy rất quan trọng: MLO trước, u-boot.img sau.

### Test U-Boot

- Rút thẻ nhớ an toàn
- Cắm vào BeagleBone Black
- Giữ nút S2 (BOOT)
- Cấp nguồn và mở terminal UART

Kết quả mong đợi: U-Boot dừng ở thông báo không tìm thấy kernel (TIMEOUT), xác nhận U-Boot hoạt động đúng.

## 5. Build Linux Kernel cho BeagleBone Black

### Chuẩn bị thư mục Kernel

```bash
mkdir -p ~/bbb/kernel
cd ~/bbb/kernel
```

### Tải Linux Kernel (Stable)

```bash
git clone https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
cd linux
git checkout v6.6
```

### Thiết lập môi trường build

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
```

### Cấu hình Kernel cho BBB

```bash
make distclean
make multi_v7_defconfig
```

Cấu hình bổ sung (nếu cần):

```bash
make menuconfig
```

Đảm bảo bật:

- AM335x SoC support
- Device Tree support
- MMC / SD support
- EXT4 filesystem

### Biên dịch Kernel

```bash
make -j$(nproc) zImage
make -j$(nproc) dtbs
```

File tạo ra:

- `arch/arm/boot/zImage`
- `arch/arm/boot/dts/am335x-boneblack.dtb`

## 6. Copy Kernel & Device Tree vào thẻ nhớ

```bash
cp arch/arm/boot/zImage /media/$USER/BOOT/
cp arch/arm/boot/dts/am335x-boneblack.dtb /media/$USER/BOOT/
sync
```

## 7. Boot Kernel bằng extlinux.conf

### Tạo cấu trúc thư mục

```bash
mkdir -p /media/$USER/BOOT/extlinux
```

### Tạo file extlinux.conf

```bash
nano /media/$USER/BOOT/extlinux/extlinux.conf
```

Nội dung:

```
TIMEOUT 1
DEFAULT Rimuru_Linux

LABEL Rimuru_Linux
    KERNEL ../zImage
    FDT ../am335x-boneblack.dtb
    APPEND console=ttyO0,115200n8 root=/dev/mmcblk0p2 rw rootwait
```

## 8. Test boot Kernel

- Rút thẻ nhớ an toàn
- Cắm vào BeagleBone Black
- Giữ nút S2 và cấp nguồn

Kết quả mong đợi:

- U-Boot tìm thấy extlinux.conf
- Kernel bắt đầu boot
- Dừng ở lỗi:

```
Kernel panic - not syncing: VFS: Unable to mount root fs
```

Lỗi này là đúng, do phân vùng ROOTFS còn trống.

## 9. Boot Kernel thủ công bằng U-Boot CMD

### Mở UART:

```bash
sudo minicom -D /dev/ttyUSB0 -b 115200
```

Nhấn Enter liên tục khi thấy:

```
Hit any key to stop autoboot:
```

### Kiểm tra thẻ SD

```
=> mmc list
=> mmc dev 0
=> ls mmc 0:1
```

Phải thấy:

- MLO
- u-boot.img
- zImage
- am335x-boneblack.dtb
- extlinux/

### Load Kernel và Device Tree

```
=> load mmc 0:1 0x82000000 zImage
=> load mmc 0:1 0x88000000 am335x-boneblack.dtb
```

### Thiết lập bootargs

```
=> setenv bootargs console=ttyO0,115200n8 root=/dev/mmcblk0p2 rw rootwait
```

### Boot Kernel

```
=> bootz 0x82000000 - 0x88000000
```

Kết quả mong đợi:

```
Starting kernel ...
```

Kernel tiếp tục boot và dừng ở panic do chưa có rootfs.

## 10. Kết luận

Trong tuần này đã thực hiện:

- Build thành công U-Boot cho BeagleBone Black
- Chuẩn bị thẻ SD đúng chuẩn phần cứng AM335x
- Build Linux Kernel cho BBB
- Boot Kernel bằng extlinux.conf và U-Boot CMD
- Hiểu rõ vai trò của bootargs và nguyên nhân Kernel Panic

Đây là nền tảng cho các tuần tiếp theo: build root filesystem, driver và hệ thống Embedded Linux hoàn chỉnh.
