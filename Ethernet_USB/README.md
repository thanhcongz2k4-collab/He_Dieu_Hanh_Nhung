# Kết Nối BeagleBone Black qua USB (RNDIS)

### *Phiên bản kernel: 6.16.5*

## Mục tiêu

Sau khi hoàn tất, BeagleBone Black (BBB) sẽ tạo mạng USB (RNDIS) và xuất hiện giao diện `usb0` với IP `192.168.7.2/24`. Máy host có thể ping và SSH vào board qua cáp USB.

---

## Tổng quan các bước

1. Bật đầy đủ tùy chọn USB Gadget/RNDIS trong kernel Buildroot
2. Tạo script init `S45usbgadget` để tự động dựng gadget khi boot
3. Thêm rootfs overlay vào Buildroot
4. Build lại image và ghi vào SD card
5. Cấu hình phía host và SSH vào board

---

## Bước 1: Bật cấu hình kernel cho USB RNDIS

```bash
cd ~/buildroot
make linux-menuconfig
```

Vào đúng menu sau và bật các mục tương ứng:

```
Device Drivers  --->
    [*] USB support  --->
        <*>   Support for Host-side USB
        <*>   USB Gadget Support  --->
                <M>   USB Gadget functions configurable through configfs
                [*]     RNDIS
        <*>   Inventra Highspeed Dual Role Controller
                MUSB Mode Selection (Dual Role mode)  --->
                <*>     OMAP2430 and onwards
                <*>     TI DSPS platforms
        USB Physical Layer drivers  --->
                <*> NOP USB Transceiver Driver
                <*> AM335x USB PHY Driver
```

---

## Bước 2: Tạo script khởi động USB gadget

```bash
mkdir -p board/beagleboard/beaglebone/rootfs_overlay/etc/init.d
nano board/beagleboard/beaglebone/rootfs_overlay/etc/init.d/S45usbgadget
```

Nội dung file `S45usbgadget`:

```bash
#!/bin/sh
#
# USB RNDIS Gadget setup via configfs
#

GADGET_DIR=/sys/kernel/config/usb_gadget
GADGET_NAME=g0
GADGET=$GADGET_DIR/$GADGET_NAME

VENDOR_ID="0x0525"
PRODUCT_ID="0xa4a2"
MANUFACTURER="BeagleBone"
PRODUCT="BeagleBone RNDIS"
SERIAL="deadbeef12345678"

# Byte đầu của DEV_ADDR nên là 0x02 (locally administered)
DEV_ADDR="02:dd:bb:cc:dd:01"
HOST_ADDR="02:dd:bb:cc:dd:02"

start() {
    echo "Starting USB RNDIS gadget..."

    modprobe libcomposite 2>/dev/null
    modprobe usb_f_rndis 2>/dev/null

    if ! grep -q configfs /proc/mounts; then
        mount -t configfs none /sys/kernel/config
    fi

    UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    if [ -z "$UDC" ]; then
        echo "ERROR: No UDC found!"
        return 1
    fi
    echo "Found UDC: $UDC"

    # Dọn dẹp gadget cũ nếu có
    if [ -d "$GADGET" ]; then
        echo "" > "$GADGET/UDC" 2>/dev/null
        sleep 0.5
        rm -rf "$GADGET/configs/c.1/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null
        rmdir "$GADGET/configs/c.1" 2>/dev/null
        rmdir "$GADGET/functions/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/strings/0x409" 2>/dev/null
        rmdir "$GADGET" 2>/dev/null
    fi

    mkdir -p "$GADGET"
    echo "$VENDOR_ID" > "$GADGET/idVendor"
    echo "$PRODUCT_ID" > "$GADGET/idProduct"
    echo "0x0200" > "$GADGET/bcdUSB"
    echo "0x0100" > "$GADGET/bcdDevice"

    mkdir -p "$GADGET/strings/0x409"
    echo "$MANUFACTURER" > "$GADGET/strings/0x409/manufacturer"
    echo "$PRODUCT" > "$GADGET/strings/0x409/product"
    echo "$SERIAL" > "$GADGET/strings/0x409/serialnumber"

    mkdir -p "$GADGET/functions/rndis.usb0"
    echo "$DEV_ADDR" > "$GADGET/functions/rndis.usb0/dev_addr"
    echo "$HOST_ADDR" > "$GADGET/functions/rndis.usb0/host_addr"

    # OS descriptor để Windows nhận đúng RNDIS
    echo "1" > "$GADGET/os_desc/use"
    echo "0xcd" > "$GADGET/os_desc/b_vendor_code"
    echo "MSFT100" > "$GADGET/os_desc/qw_sign"
    echo "RNDIS" > "$GADGET/functions/rndis.usb0/os_desc/interface.rndis/compatible_id"
    echo "5162001" > "$GADGET/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id"

    mkdir -p "$GADGET/configs/c.1"
    echo 500 > "$GADGET/configs/c.1/MaxPower"
    mkdir -p "$GADGET/configs/c.1/strings/0x409"
    echo "RNDIS Config" > "$GADGET/configs/c.1/strings/0x409/configuration"

    ln -s "$GADGET/configs/c.1" "$GADGET/os_desc/c.1"
    ln -s "$GADGET/functions/rndis.usb0" "$GADGET/configs/c.1/rndis.usb0"

    echo "$UDC" > "$GADGET/UDC"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to bind gadget to UDC $UDC"
        return 1
    fi

    echo "USB RNDIS gadget started, UDC: $UDC"

    sleep 1
    if ip link show usb0 > /dev/null 2>&1; then
        ip addr add 192.168.7.2/24 dev usb0
        ip link set usb0 up
        echo "usb0 configured: 192.168.7.2/24"
    else
        echo "WARNING: usb0 interface not found yet"
    fi
}

stop() {
    echo "Stopping USB RNDIS gadget..."

    ip link set usb0 down 2>/dev/null
    ip addr flush dev usb0 2>/dev/null

    if [ -d "$GADGET" ]; then
        echo "" > "$GADGET/UDC" 2>/dev/null
        sleep 0.5
        rm -f "$GADGET/os_desc/c.1" 2>/dev/null
        rm -f "$GADGET/configs/c.1/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null
        rmdir "$GADGET/configs/c.1" 2>/dev/null
        rmdir "$GADGET/functions/rndis.usb0" 2>/dev/null
        rmdir "$GADGET/strings/0x409" 2>/dev/null
        rmdir "$GADGET/os_desc" 2>/dev/null
        rmdir "$GADGET" 2>/dev/null
    fi
    echo "USB RNDIS gadget stopped"
}

case "$1" in
    start) start ;;
    stop) stop ;;
    restart) stop; start ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac

exit 0
```

Cấp quyền thực thi:

```bash
chmod +x board/beagleboard/beaglebone/rootfs_overlay/etc/init.d/S45usbgadget
```

---

## Bước 3: Thêm RootFS Overlay vào Buildroot

```bash
make menuconfig
```

Vào `System configuration` → `Root filesystem overlay directories` và điền:

```
board/beagleboard/beaglebone/rootfs_overlay
```

Đặt mật khẩu root để SSH sau này:

```
System configuration  --->
    Root password: <your_password_here>
```

Bật Dropbear làm SSH server (nhẹ, phù hợp cho embedded):

```
Target packages  --->
    Networking applications  --->
        [*] dropbear
        [ ] openssh    ← để trống, chỉ chọn một
```

---

## Bước 4: Build lại và ghi image

```bash
cd ~/buildroot
make
```

Kiểm tra đúng thiết bị thẻ SD trước khi ghi:

```bash
lsblk
```

Ghi image (thay `/dev/sdX` bằng đúng thiết bị):

```bash
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

> ⚠️ Kiểm tra kỹ `/dev/sdX` trước khi ghi để tránh ghi nhầm ổ cứng.

---

## Bước 5: Kết nối từ máy host

Boot BBB trước, chờ 30–60 giây, rồi cắm cáp USB vào máy host.

> ⚠️ Dùng cáp USB data, không dùng cáp chỉ sạc.

---

### 🪟 Trường hợp dùng Windows

**1. Kiểm tra driver**

Vào `Device Manager`, nếu thấy thiết bị báo lỗi driver thì cài thủ công:
- Click chuột phải → `Update driver` → `Browse my computer` → `Let me pick`
- Chọn `Network Adapters` → `Microsoft` → `Remote NDIS Compatible Device`

**2. Đặt IP tĩnh cho adapter**

Vào `Control Panel` → `Network and Internet` → `Network Connections`, tìm adapter mới xuất hiện (thường tên là "Remote NDIS based Internet Sharing Device").

- Click chuột phải → `Properties` → `Internet Protocol Version 4 (TCP/IPv4)` → `Properties`
- Chọn `Use the following IP address`:
  - IP address: `192.168.7.1`
  - Subnet mask: `255.255.255.0`
  - Default gateway: để trống
- Lưu lại

**3. Ping và SSH**

Mở Command Prompt hoặc PowerShell:

```
ping 192.168.7.2
ssh root@192.168.7.2
```

Hoặc dùng PuTTY với hostname `192.168.7.2`, port `22`.

**Kết nối lại sau reboot Windows:** IP tĩnh đã được lưu, chỉ cần cắm cáp USB là dùng được ngay.

---

### 🐧 Trường hợp dùng Ubuntu

**1. Kiểm tra interface**

Sau khi cắm cáp USB, chạy:

```bash
ip link show
```

Tìm interface có dạng `enx02ddbbccdd02` (tên dựa theo MAC address). Ubuntu không đặt tên là `usb0` như trên BBB.

**2. Đặt IP tĩnh**

Thay `enx02ddbbccdd02` bằng tên interface thực tế trên máy bạn:

```bash
sudo ip addr add 192.168.7.1/24 dev enx02ddbbccdd02
sudo ip link set enx02ddbbccdd02 up
```

**3. Ping và SSH**

```bash
ping 192.168.7.2
ssh root@192.168.7.2
```

Lần đầu SSH sẽ hỏi xác nhận fingerprint, gõ `yes` rồi nhập mật khẩu root.

**Kết nối lại sau reboot Ubuntu:** IP sẽ mất sau mỗi lần reboot, có hai cách xử lý:

*Cách 1 — Gõ lại lệnh mỗi lần:*

```bash
sudo ip addr add 192.168.7.1/24 dev enx02ddbbccdd02
sudo ip link set enx02ddbbccdd02 up
ssh root@192.168.7.2
```

*Cách 2 — Tự động qua Netplan (khuyến nghị):*

```bash
sudo nano /etc/netplan/99-beaglebone.yaml
```

Nội dung:

```yaml
network:
  version: 2
  ethernets:
    enx02ddbbccdd02:
      dhcp4: no
      addresses:
        - 192.168.7.1/24
```

Áp dụng:

```bash
sudo netplan apply
```

Từ đó chỉ cần cắm cáp USB là SSH vào được ngay.

---

## Lỗi thường gặp

**1. Không thấy UDC (`No UDC found`)**
- Kiểm tra cáp USB data (không dùng cáp chỉ sạc)
- Kiểm tra kernel đã bật USB gadget đúng chưa

**2. Windows nhận sai driver (USB Serial thay vì RNDIS)**
- Kiểm tra phần `os_desc` trong script có đầy đủ không
- Rút cáp USB cắm lại sau khi reboot board
- Cài driver thủ công như hướng dẫn ở Bước 5 Windows

**3. Không thấy interface trên Ubuntu**
- Chờ thêm 10–20 giây sau khi cắm cáp
- Chạy lại script trên BBB: `/etc/init.d/S45usbgadget restart`
- Xem log kernel trên BBB: `dmesg | tail -n 100`

**4. Ping không được dù interface đã có IP (Ubuntu)**
- Chắc chắn đã gán IP cho đúng tên interface
- Kiểm tra firewall: `sudo ufw status`

**5. Lỗi `duplicate_label` khi build kernel**

Xảy ra khi thêm node i2c vào DTS trong khi `am335x-bone-common.dtsi` đã khai báo sẵn. Kernel 6.16.5 kiểm tra nghiêm hơn các phiên bản cũ.

Sửa bằng cách bỏ khai báo pinctrl trùng lặp, chỉ giữ phần device con:

```dts
&i2c2 {
    your-device@addr {
        compatible = "vendor,device";
        reg = <0xaddr>;
    };
};
```

Không cần khai báo lại `status = "okay"` hay `pinctrl-0` vì đã có sẵn trong dtsi gốc.
