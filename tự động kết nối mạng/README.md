# BBB Auto Network Setup (udev)

Tự động cấu hình mạng khi cắm BeagleBone Black vào máy Host qua USB RNDIS.

## Tính năng

- Tự động set IP `192.168.7.1/24` cho interface `enx02ddbbccdd02` khi cắm BBB
- Tự động bật IP forwarding và NAT để BBB có thể dùng Internet từ WiFi của Host
- Giám sát kết nối mỗi 5 giây, tự phục hồi nếu mất kết nối
- Tự động dọn sạch iptables rules khi rút USB

---

## Cấu trúc file

```
/etc/udev/rules.d/99-bbb.rules     ← udev rule phát hiện USB
/usr/local/bin/bbb-net.sh          ← Script khởi động mạng
/usr/local/bin/bbb-net-stop.sh     ← Script dọn dẹp khi rút USB
```

---

## Cài đặt

### Bước 1: Tạo udev rule

```sh
sudo nano /etc/udev/rules.d/99-bbb.rules
```

Nội dung:
```
ACTION=="add", SUBSYSTEM=="net", KERNEL=="enx02ddbbccdd02", RUN+="/usr/local/bin/bbb-net.sh"
ACTION=="move", SUBSYSTEM=="net", KERNEL=="enx02ddbbccdd02", RUN+="/usr/local/bin/bbb-net.sh"
ACTION=="remove", SUBSYSTEM=="net", KERNEL=="enx02ddbbccdd02", RUN+="/usr/local/bin/bbb-net-stop.sh"
```

> **Lưu ý:** Cần bắt cả `add` và `move` vì kernel tạo interface tên `usb0` trước, sau đó đổi tên thành `enx02ddbbccdd02`.

### Bước 2: Tạo script khởi động

```sh
sudo nano /usr/local/bin/bbb-net.sh
```

Nội dung:
```sh
#!/bin/sh
sleep 1

ip addr add 192.168.7.1/24 dev enx02ddbbccdd02
ip link set enx02ddbbccdd02 up

sysctl -w net.ipv4.ip_forward=1

iptables -t nat -A POSTROUTING -o wlp0s20f3 -j MASQUERADE
iptables -A FORWARD -i enx02ddbbccdd02 -o wlp0s20f3 -j ACCEPT
iptables -A FORWARD -i wlp0s20f3 -o enx02ddbbccdd02 -m state \
    --state RELATED,ESTABLISHED -j ACCEPT

# Giám sát liên tục trong nền
(
    while ip link show enx02ddbbccdd02 > /dev/null 2>&1; do
        if ! ping -c 1 -W 2 192.168.7.2 > /dev/null 2>&1; then
            ip addr flush dev enx02ddbbccdd02
            ip addr add 192.168.7.1/24 dev enx02ddbbccdd02
            ip link set enx02ddbbccdd02 up
        fi
        sleep 5
    done
) &
```

### Bước 3: Tạo script dọn dẹp

```sh
sudo nano /usr/local/bin/bbb-net-stop.sh
```

Nội dung:
```sh
#!/bin/sh
iptables -t nat -D POSTROUTING -o wlp0s20f3 -j MASQUERADE
iptables -D FORWARD -i enx02ddbbccdd02 -o wlp0s20f3 -j ACCEPT
iptables -D FORWARD -i wlp0s20f3 -o enx02ddbbccdd02 -m state \
    --state RELATED,ESTABLISHED -j ACCEPT
```

### Bước 4: Cấp quyền và reload

```sh
sudo chmod +x /usr/local/bin/bbb-net.sh
sudo chmod +x /usr/local/bin/bbb-net-stop.sh
sudo udevadm control --reload-rules
```

---

## Cách hoạt động

```
Cắm USB BBB vào Host
        ↓
Kernel tạo interface usb0
        ↓
Kernel đổi tên → enx02ddbbccdd02
        ↓
udev phát hiện ACTION==move → chạy bbb-net.sh
        ↓
Set IP 192.168.7.1/24, bật forwarding, cấu hình iptables NAT
        ↓
Vòng lặp ngầm chạy, ping 192.168.7.2 mỗi 5 giây
        │
        ├── Ping OK     → chờ 5 giây, kiểm tra lại
        │
        └── Ping thất bại → flush IP, set lại IP, link up
        ↓
Rút USB BBB
        ↓
udev phát hiện ACTION==remove → chạy bbb-net-stop.sh
        ↓
Xóa sạch iptables rules, vòng lặp tự thoát
```

---

## Kiểm tra

Sau khi cắm BBB, kiểm tra IP đã được set chưa:
```sh
ip addr show enx02ddbbccdd02
# Phải thấy inet 192.168.7.1/24
```

SSH vào BBB:
```sh
ssh root@192.168.7.2
```

---

## Lưu ý

- Interface WiFi mặc định là `wlp0s20f3`, thay đổi nếu tên khác trên máy bạn
- IP của Host: `192.168.7.1`, IP của BBB: `192.168.7.2`
- Script chạy với quyền root do udev kích hoạt, không cần `sudo` trong script
