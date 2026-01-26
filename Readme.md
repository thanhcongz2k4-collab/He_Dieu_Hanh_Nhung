# TUẦN 1 – BUILD TOOLCHAIN CROSS-COMPILE ARM BẰNG CROSSTOOL-NG & CHẠY VỚI QEMU - Nhóm 2

## 1. Mục tiêu

Sau khi hoàn thành tuần 1, sinh viên sẽ:

- Hiểu quy trình build toolchain cross-compile thủ công bằng Crosstool-NG
- Cấu hình và build toolchain ARM + musl libc
- Sử dụng toolchain để compile chương trình C cho ARM
- Chạy và kiểm tra chương trình ARM trên máy x86 bằng QEMU user-mode + chroot

---

## 2. Chuẩn bị dữ liệu thực hành 

Các bài lab sử dụng bộ dữ liệu chuẩn do Bootlin cung cấp.

Thực hiện trên terminal:

```bash
cd
wget https://bootlin.com/doc/training/embedded-linux-bbb/embedded-linux-bbb-labs.tar.xz
tar xvf embedded-linux-bbb-labs.tar.xz
```

Sau khi giải nén, thư mục sau sẽ xuất hiện:

```text
$HOME/embedded-linux-bbb-labs/
```

Thư mục này sẽ được sử dụng xuyên suốt các bài lab.

---

## 3. Cập nhật hệ thống

Để tránh lỗi package trong quá trình build toolchain:

```bash
sudo apt update
sudo apt dist-upgrade
```

Khuyến nghị reboot sau khi update.

---

## 4. Chuẩn bị môi trường Crosstool-NG

### 4.1 Di chuyển vào thư mục toolchain

```bash
cd $HOME/embedded-linux-bbb-labs/toolchain
```

### 4.2 Yêu cầu hệ thống

- RAM tối thiểu: 4GB (khuyến nghị 8GB)
- Dung lượng trống: \~15–20GB

### 4.3 Cài đặt các package cần thiết

```bash
sudo apt install build-essential git autoconf bison flex texinfo help2man gawk \
libtool-bin libncurses5-dev unzip
```

---

## 5. Tải Crosstool-NG

Clone source code và checkout phiên bản đã được kiểm thử:

```bash
git clone https://github.com/crosstool-ng/crosstool-ng
cd crosstool-ng
git checkout crosstool-ng-1.26.0
```

---

## 6. Build và cài đặt Crosstool-NG

Do build từ source git (không phải release archive), cần chạy bootstrap:

```bash
./bootstrap
```

Cấu hình Crosstool-NG theo kiểu cài đặt local:

```bash
./configure --enable-local
make
```

Sau khi build xong, công cụ `ct-ng` sẽ nằm ngay trong thư mục này.

---

## 7. Cấu hình toolchain ARM

Chạy giao diện cấu hình:

```bash
./ct-ng menuconfig
```

### 7.1 Các thiết lập quan trọng

#### Path and misc options

- Enable: **Try features marked as EXPERIMENTAL**
- Nếu hệ thống dùng `wget2`, bỏ chọn tùy chọn `--passive-ftp`

#### Target options

- Target Architecture: **arm**
- Use specific FPU: **vfpv3**
- Floating point: **hardware (FPU)**

#### Toolchain options

- Tuple vendor string: **training**
- Tuple alias: **arm-linux**

#### Operating System

- Target OS: **linux**
- Linux kernel headers: chọn phiên bản **<= 6.6**

#### C library

- Chọn: **musl**

#### C compiler

- GCC version: **13.2.0**
- Enable C++ support

#### Debug facilities

- Bỏ chọn **toàn bộ** (giảm thời gian build)

Lưu cấu hình và thoát menuconfig.

---

## 8. Build toolchain

Chạy lệnh build:

```bash
./ct-ng build
```

⏱ Thời gian build: **30–60 phút** (tùy cấu hình máy).

Sau khi hoàn tất, toolchain được cài tại:

```text
$HOME/x-tools/arm-training-linux-musleabihf/
```

---

## 9. Kiểm tra toolchain

### 9.1 Thêm toolchain vào PATH

```bash
export PATH=$HOME/x-tools/arm-training-linux-musleabihf/bin:$PATH
```

### 9.2 Kiểm tra phiên bản compiler

```bash
arm-training-linux-musleabihf-gcc --version
```

---

## 10. Compile chương trình C cho ARM

### 10.1 Ví dụ chương trình test

```c
#include <stdio.h>


⏱ Thời gian build: 30–60 phút (tùy cấu hình máy).
    return 0;
}
```

### 10.2 Compile bằng toolchain mới

```bash
arm-training-linux-musleabihf-gcc hello.c -o hello_arm
```

### 10.3 Kiểm tra file binary

```bash
file hello_arm
```

Hoặc:

```bash
arm-training-linux-musleabihf-readelf -h hello_arm
```

Kết quả mong đợi: ELF 32-bit, ARM, dynamically linked với musl.

---

## 11. MỞ RỘNG – Chạy chương trình ARM bằng QEMU

### 11.1 Cài QEMU user-mode trên host

```bash
sudo apt install qemu-user-static binfmt-support
```

Kiểm tra:

```bash
qemu-arm-static --version
```

---

### 11.2 Chuẩn bị Alpine Linux ARM rootfs

```bash
cd ~
wget https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/armhf/alpine-minirootfs-3.23.2-armhf.tar.gz
mkdir alpine-armhf
sudo tar -xzf alpine-minirootfs-3.23.2-armhf.tar.gz -C alpine-armhf
```

Copy QEMU vào rootfs:

```bash
sudo cp /usr/bin/qemu-arm-static alpine-armhf/usr/bin/
```

Bind các filesystem cần thiết:

```bash
sudo mount --bind /dev  alpine-armhf/dev
sudo mount --bind /proc alpine-armhf/proc
sudo mount --bind /sys  alpine-armhf/sys
```

---

### 11.3 Copy chương trình ARM vào rootfs

```bash
sudo cp hello_arm alpine-armhf/root/
sudo chmod +x alpine-armhf/root/hello_arm
```

---

### 11.4 Chroot và chạy chương trình

```bash
sudo chroot alpine-armhf /bin/sh
```

Kiểm tra kiến trúc:

```bash
uname -m
```

Kết quả mong đợi:

```text
armv7l
```

Chạy chương trình:

```bash
cd /root
./hello_arm
```

---

## 12. Kết luận

Trong tuần 1, đã thực hiện:

- Build toolchain cross-compile ARM thủ công bằng Crosstool-NG
- Sử dụng thư viện C musl
- Compile chương trình ARM trên host x86
- Chạy và kiểm tra chương trình ARM bằng QEMU

Đây là nền tảng quan trọng cho các tuần tiếp theo (kernel, rootfs, driver, embedded Linux system).

