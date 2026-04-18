# PWM RGB LED Driver cho BeagleBone Black (Buildroot)

## Tổng quan

Driver điều khiển RGB LED bằng 3 kênh PWM (EHRPWM) trên BeagleBone Black, chạy trên Linux kernel 6.x build bằng Buildroot.

---

## Phần cứng

### Kết nối chân

Dùng RGB LED **common cathode** + điện trở 220Ω mỗi chân:

```
BBB Header          RGB LED
──────────────────────────────
P8_19 (ehrpwm2A) ──[220Ω]── RED
P8_13 (ehrpwm2B) ──[220Ω]── GREEN
P9_14 (ehrpwm1A) ──[220Ω]── BLUE
P9_1  (GND)      ────────── GND (chân chung)
```

> Nếu dùng **common anode**: cắm chân chung vào P9_3 (3.3V) và đổi PWM flags thành `1` trong DTS.

---

## Cấu trúc thư mục

```
buildroot/
└── package/
    ├── pwm_rgb/
    │   ├── src/
    │   │   ├── pwm_rgb.c       ← Kernel module driver
    │   │   └── Makefile        ← Build kernel module
    │   ├── Config.in
    │   └── pwm_rgb.mk
    └── pwm_test/
        ├── src/
        │   └── pwm_test.c      ← Userspace test app
        ├── Config.in
        └── pwm_test.mk
```

---

## Bước 1 — Cấu hình Device Tree

### `am335x-bone-common.dtsi` — Thêm pinmux

Thêm vào trong block `&am33xx_pinmux`:

```dts
ehrpwm1_pins: ehrpwm1-pins {
    pinctrl-single,pins = <
        AM33XX_PADCONF(AM335X_PIN_GPMC_A2, PIN_OUTPUT, MUX_MODE6)  /* P9_14 ehrpwm1A */
        AM33XX_PADCONF(AM335X_PIN_GPMC_A3, PIN_OUTPUT, MUX_MODE6)  /* P9_16 ehrpwm1B */
    >;
};

ehrpwm2_pins: ehrpwm2-pins {
    pinctrl-single,pins = <
        AM33XX_PADCONF(AM335X_PIN_GPMC_AD8, PIN_OUTPUT, MUX_MODE4)  /* P8_19 ehrpwm2A */
        AM33XX_PADCONF(AM335X_PIN_GPMC_AD9, PIN_OUTPUT, MUX_MODE4)  /* P8_13 ehrpwm2B */
    >;
};
```

### `am335x-boneblack.dts` — Enable PWM + khai báo RGB LED

```dts
/* Enable EHRPWM subsystems */
&epwmss1 {
    status = "okay";
};

&ehrpwm1 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&ehrpwm1_pins>;
};

&epwmss2 {
    status = "okay";
};

&ehrpwm2 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&ehrpwm2_pins>;
};

/* RGB LED device */
/ {
    rgb_led: rgb-led {
        compatible = "my,pwm-rgb";

        pwms = <&ehrpwm2 0 500000 0>,   /* RED   — P8_19 */
               <&ehrpwm2 1 500000 0>,   /* GREEN — P8_13 */
               <&ehrpwm1 0 500000 0>;   /* BLUE  — P9_14 */

        pwm-names = "red", "green", "blue";
    };
};
```

---

## Bước 2 — Cấu hình Kernel

```bash
make linux-menuconfig
```

Bật các option sau:

```
Device Drivers
  └── Pulse-Width Modulation (PWM) Support
        └── [*] EHRPWM PWM support          (CONFIG_PWM_TIEHRPWM)
        └── [*] ECAP PWM support            (CONFIG_PWM_ECAP)

Device Drivers
  └── LED Support
        └── [*] LED Class Support           (CONFIG_LEDS_CLASS)
```

---

## Bước 3 — Kernel Module (`pwm_rgb`)

### `package/pwm_rgb/src/pwm_rgb.c`

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * PWM RGB LED Driver for BeagleBone Black
 * Compatible: "my,pwm-rgb"
 *
 * Exposes 3 sysfs entries:
 *   /sys/class/leds/rgb:red/brightness   (0-255)
 *   /sys/class/leds/rgb:green/brightness (0-255)
 *   /sys/class/leds/rgb:blue/brightness  (0-255)
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/slab.h>

#define PWM_PERIOD_NS   500000  /* 2 kHz */

/* Mỗi channel RGB là 1 led_pwm */
struct pwm_rgb_led {
    struct led_classdev cdev;   /* LED class device → tạo /sys/class/leds/... */
    struct pwm_device   *pwm;   /* PWM handle lấy từ DTS */
};

/* Driver data gồm 3 channel */
struct pwm_rgb_priv {
    struct pwm_rgb_led leds[3]; /* 0=R, 1=G, 2=B */
};

/* ------------------------------------------------------------------ */
/* Callback gọi khi userspace write vào brightness                     */
/* ------------------------------------------------------------------ */
static void pwm_rgb_set_brightness(struct led_classdev *cdev,
                                   enum led_brightness brightness)
{
    struct pwm_rgb_led *led = container_of(cdev, struct pwm_rgb_led, cdev);
    struct pwm_state state;
    u64 duty;

    /* Tính duty cycle: brightness 0-255 → 0 đến PWM_PERIOD_NS */
    duty = (u64)brightness * PWM_PERIOD_NS / 255;

    pwm_get_state(led->pwm, &state);
    state.duty_cycle = duty;
    state.period     = PWM_PERIOD_NS;
    state.enabled    = (brightness > 0);

    /* kernel >= 6.x: pwm_apply_might_sleep thay thế pwm_apply_state */
    pwm_apply_might_sleep(led->pwm, &state);
}

/* ------------------------------------------------------------------ */
/* probe: gọi khi kernel match DTS node với driver này                 */
/* ------------------------------------------------------------------ */
static int pwm_rgb_probe(struct platform_device *pdev)
{
    struct pwm_rgb_priv *priv;
    struct device *dev = &pdev->dev;
    const char *names[3]     = { "rgb:red", "rgb:green", "rgb:blue" };
    const char *pwm_names[3] = { "red", "green", "blue" };
    int i, ret;

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    for (i = 0; i < 3; i++) {
        /* Lấy PWM theo tên khai báo trong DTS: pwm-names = "red","green","blue" */
        priv->leds[i].pwm = devm_pwm_get(dev, pwm_names[i]);
        if (IS_ERR(priv->leds[i].pwm)) {
            dev_err(dev, "Failed to get PWM '%s': %ld\n",
                    pwm_names[i], PTR_ERR(priv->leds[i].pwm));
            return PTR_ERR(priv->leds[i].pwm);
        }

        /* Cấu hình LED class device */
        priv->leds[i].cdev.name           = names[i];
        priv->leds[i].cdev.brightness     = LED_OFF;
        priv->leds[i].cdev.max_brightness = 255;
        priv->leds[i].cdev.brightness_set = pwm_rgb_set_brightness;

        ret = led_classdev_register(dev, &priv->leds[i].cdev);
        if (ret) {
            dev_err(dev, "Failed to register LED '%s': %d\n", names[i], ret);
            return ret;
        }

        dev_info(dev, "Registered LED: %s\n", names[i]);
    }

    platform_set_drvdata(pdev, priv);
    dev_info(dev, "PWM RGB driver probed OK\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* remove: kernel >= 6.x đổi sang void, không return int              */
/* ------------------------------------------------------------------ */
static void pwm_rgb_remove(struct platform_device *pdev)
{
    struct pwm_rgb_priv *priv = platform_get_drvdata(pdev);
    struct pwm_state state;
    int i;

    for (i = 0; i < 3; i++) {
	led_classdev_unregister(&priv->leds[i].cdev); 
        pwm_get_state(priv->leds[i].pwm, &state);
        state.enabled = false;
        pwm_apply_might_sleep(priv->leds[i].pwm, &state);
    }
}

/* ------------------------------------------------------------------ */
/* DTS match table — phải khớp với compatible trong .dts               */
/* ------------------------------------------------------------------ */
static const struct of_device_id pwm_rgb_of_match[] = {
    { .compatible = "my,pwm-rgb" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_rgb_of_match);

static struct platform_driver pwm_rgb_driver = {
    .probe  = pwm_rgb_probe,
    .remove = pwm_rgb_remove,
    .driver = {
        .name           = "pwm-rgb",
        .of_match_table = pwm_rgb_of_match,
    },
};

module_platform_driver(pwm_rgb_driver);

MODULE_AUTHOR("BBB Developer");
MODULE_DESCRIPTION("PWM RGB LED Driver for BeagleBone Black");
MODULE_LICENSE("GPL v2");
```

### `package/pwm_rgb/src/Makefile`

```makefile
obj-m += pwm_rgb.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
```

### `package/pwm_rgb/Config.in`

```
config BR2_PACKAGE_PWM_RGB
	bool "pwm_rgb"
	depends on BR2_LINUX_KERNEL
	help
	  PWM RGB LED kernel module driver for BeagleBone Black.
```

### `package/pwm_rgb/pwm_rgb.mk`

```makefile
PWM_RGB_VERSION     = 1.0
PWM_RGB_SITE        = $(TOPDIR)/package/pwm_rgb/src
PWM_RGB_SITE_METHOD = local

define PWM_RGB_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE=$(TARGET_CROSS) \
		modules
endef

define PWM_RGB_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pwm_rgb.ko \
		$(TARGET_DIR)/lib/modules/pwm_rgb.ko
endef

$(eval $(generic-package))
```

---

## Bước 4 — Userspace App (`pwm_test`)

### `package/pwm_test/pwm_test.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>

#define SYSFS_RED   "/sys/class/leds/rgb:red/brightness"
#define SYSFS_GREEN "/sys/class/leds/rgb:green/brightness"
#define SYSFS_BLUE  "/sys/class/leds/rgb:blue/brightness"

static volatile int running = 1;

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Mở file descriptor 1 lần, tái dụng lại cho tốc độ cao              */
/* ------------------------------------------------------------------ */
static int fd_r = -1, fd_g = -1, fd_b = -1;

static void open_leds(void)
{
    fd_r = open(SYSFS_RED,   O_WRONLY);
    fd_g = open(SYSFS_GREEN, O_WRONLY);
    fd_b = open(SYSFS_BLUE,  O_WRONLY);

    if (fd_r < 0 || fd_g < 0 || fd_b < 0) {
        perror("open sysfs led");
        exit(1);
    }
}

static void close_leds(void)
{
    if (fd_r >= 0) close(fd_r);
    if (fd_g >= 0) close(fd_g);
    if (fd_b >= 0) close(fd_b);
}

static inline void write_brightness(int fd, int val)
{
    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", val);
    lseek(fd, 0, SEEK_SET);
    (void)write(fd, buf, len);
}

static void set_rgb(int r, int g, int b)
{
    write_brightness(fd_r, r);
    write_brightness(fd_g, g);
    write_brightness(fd_b, b);
}

static void all_off(void)
{
    set_rgb(0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* HSV → RGB                                                           */
/* h: 0.0 ~ 360.0  s: 0.0 ~ 1.0  v: 0.0 ~ 1.0                       */
/* ------------------------------------------------------------------ */
static void hsv_to_rgb(float h, float s, float v,
                       int *r, int *g, int *b)
{
    float c  = v * s;
    float x  = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m  = v - c;
    float r1, g1, b1;

    if      (h <  60) { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }

    *r = (int)((r1 + m) * 255.0f + 0.5f);
    *g = (int)((g1 + m) * 255.0f + 0.5f);
    *b = (int)((b1 + m) * 255.0f + 0.5f);
}

/* ------------------------------------------------------------------ */
/* Rainbow loop                                                         */
/* step_deg: bước hue mỗi frame (nhỏ = mịn)                           */
/* delay_us: delay mỗi frame (microsecond)                             */
/* ------------------------------------------------------------------ */
static void rainbow(float step_deg, int delay_us, float sat, float val)
{
    float hue = 0.0f;
    int r, g, b;

    printf("Rainbow running... (Ctrl+C to stop)\n");
    printf("Step=%.3f deg  Delay=%d us  S=%.2f  V=%.2f\n",
           step_deg, delay_us, sat, val);

    signal(SIGINT,  handle_sigint);
    signal(SIGTERM, handle_sigint);

    while (running) {
        hsv_to_rgb(hue, sat, val, &r, &g, &b);
        set_rgb(r, g, b);

        hue += step_deg;
        if (hue >= 360.0f)
            hue -= 360.0f;

        usleep(delay_us);
    }

    all_off();
    printf("\nStopped.\n");
}

/* ------------------------------------------------------------------ */
/* Demo: chạy qua các màu cơ bản                                       */
/* ------------------------------------------------------------------ */
static void demo_colors(void)
{
    printf("=== Demo colors ===\n");

    printf("RED\n");       set_rgb(255,   0,   0); sleep(1);
    printf("GREEN\n");     set_rgb(  0, 255,   0); sleep(1);
    printf("BLUE\n");      set_rgb(  0,   0, 255); sleep(1);
    printf("YELLOW\n");    set_rgb(255, 255,   0); sleep(1);
    printf("CYAN\n");      set_rgb(  0, 255, 255); sleep(1);
    printf("MAGENTA\n");   set_rgb(255,   0, 255); sleep(1);
    printf("WHITE\n");     set_rgb(255, 255, 255); sleep(1);
    printf("OFF\n");       all_off();
}

/* ------------------------------------------------------------------ */
/* Demo: fade in/out RED                                               */
/* ------------------------------------------------------------------ */
static void demo_fade(void)
{
    int i;
    printf("=== Fade RED ===\n");

    for (i = 0; i <= 255; i += 5) {
        write_brightness(fd_r, i);
        usleep(20000);
    }
    for (i = 255; i >= 0; i -= 5) {
        write_brightness(fd_r, i);
        usleep(20000);
    }
    all_off();
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s demo                        - chay qua 7 mau co ban\n", prog);
    printf("  %s fade                        - fade in/out RED\n", prog);
    printf("  %s set R G B                   - dat mau (0-255)\n", prog);
    printf("  %s off                         - tat het\n", prog);
    printf("  %s rainbow [step] [delay_ms] [sat] [val]\n", prog);
    printf("       step    : buoc hue moi frame (default 0.5 deg)\n");
    printf("       delay_ms: delay moi frame    (default 10 ms)\n");
    printf("       sat     : saturation 0.0-1.0 (default 1.0)\n");
    printf("       val     : brightness 0.0-1.0 (default 1.0)\n");
    printf("\nVi du:\n");
    printf("  %s rainbow              # min, vua phai\n", prog);
    printf("  %s rainbow 0.1 5        # rat min, nhanh\n", prog);
    printf("  %s rainbow 1.0 5 1.0 0.5  # nhanh, toi 50%%\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    open_leds();

    if (strcmp(argv[1], "demo") == 0) {
        demo_colors();

    } else if (strcmp(argv[1], "fade") == 0) {
        demo_fade();

    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s set R G B\n", argv[0]);
            close_leds();
            return 1;
        }
        int r = atoi(argv[2]);
        int g = atoi(argv[3]);
        int b = atoi(argv[4]);
        printf("Set RGB(%d, %d, %d)\n", r, g, b);
        set_rgb(r, g, b);

    } else if (strcmp(argv[1], "off") == 0) {
        all_off();
        printf("OFF\n");

    } else if (strcmp(argv[1], "rainbow") == 0) {
        float step  = argc >= 3 ? atof(argv[2]) : 0.5f;
        int   delay = argc >= 4 ? atoi(argv[3]) * 1000 : 10000;
        float sat   = argc >= 5 ? atof(argv[4]) : 1.0f;
        float val   = argc >= 6 ? atof(argv[5]) : 1.0f;

        /* Clamp */
        if (step < 0.01f) step = 0.01f;
        if (sat  < 0.0f)  sat  = 0.0f;
        if (sat  > 1.0f)  sat  = 1.0f;
        if (val  < 0.0f)  val  = 0.0f;
        if (val  > 1.0f)  val  = 1.0f;

        rainbow(step, delay, sat, val);

    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        close_leds();
        return 1;
    }

    close_leds();
    return 0;
}
```

### `package/pwm_test/Config.in`

```
config BR2_PACKAGE_PWM_TEST
	bool "pwm_test"
	help
	  Userspace test application for PWM RGB LED.
```

### `package/pwm_test/pwm_test.mk`

```makefile
PWM_TEST_VERSION     = 1.0
PWM_TEST_SITE        = $(TOPDIR)/package/pwm_test/src
PWM_TEST_SITE_METHOD = local

define PWM_TEST_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		$(@D)/pwm_test.c \
		-o $(@D)/pwm_test \
		-lm
endef

define PWM_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pwm_test $(TARGET_DIR)/usr/bin/pwm_test
endef

$(eval $(generic-package))
```

---

## Bước 5 — Đăng ký package với Buildroot

Thêm vào `package/Config.in`:

```
source "package/pwm_rgb/Config.in"
source "package/pwm_test/Config.in"
```

Bật trong menuconfig:

```bash
make menuconfig
# Target packages → pwm_rgb → [*]
# Target packages → pwm_test → [*]
```

---

## Bước 6 — Build

```bash
# Build kernel + DTB
make linux-rebuild

# Build packages
make pwm_rgb-rebuild
make pwm_test-rebuild

# Pack image
make
```

---

## Bước 7 — Deploy lên board

### Copy nhanh qua SSH (không cần flash lại)

```bash
# Thiết lập network (USB RNDIS)
sudo ip addr add 192.168.7.1/24 dev <interface>
sudo ip link set <interface> up

# Copy file
scp output/target/usr/bin/pwm_test root@192.168.7.2:/usr/bin/
scp output/build/pwm_rgb-1.0/pwm_rgb.ko root@192.168.7.2:/lib/modules/
```

---

## Bước 8 — Test trên board

```bash
# Load driver
insmod /lib/modules/pwm_rgb.ko

# Kiểm tra probe thành công
dmesg | grep pwm
# Output mong đợi:
# pwm-rgb rgb-led: Registered LED: rgb:red
# pwm-rgb rgb-led: Registered LED: rgb:green
# pwm-rgb rgb-led: Registered LED: rgb:blue
# pwm-rgb rgb-led: PWM RGB driver probed OK

# Kiểm tra sysfs
ls /sys/class/leds/
# rgb:red  rgb:green  rgb:blue  mmc0::  mmc1::
```

### Test bằng sysfs trực tiếp

```bash
echo 255 > /sys/class/leds/rgb:red/brightness    # bật đỏ
echo 255 > /sys/class/leds/rgb:green/brightness  # bật xanh lá
echo 255 > /sys/class/leds/rgb:blue/brightness   # bật xanh dương
echo 0   > /sys/class/leds/rgb:red/brightness    # tắt đỏ
```

### Test bằng pwm_test app

```bash
pwm_test demo                    # demo 7 màu cơ bản
pwm_test fade                    # fade in/out RED
pwm_test set 255 0 0             # đặt màu đỏ
pwm_test set 255 128 0           # màu cam
pwm_test off                     # tắt hết

# Rainbow — chuyển màu cầu vồng liên tục
pwm_test rainbow                 # mặc định: mịn, vừa phải
pwm_test rainbow 0.1 5           # rất mịn, nhanh
pwm_test rainbow 1.0 5 1.0 0.5  # nhanh, độ sáng 50%
pwm_test rainbow 0.3 15 0.8 1.0 # màu pastel
# Ctrl+C để dừng
```

### Tham số rainbow

| Tham số | Ý nghĩa | Mặc định |
|---------|---------|---------|
| step | Bước hue mỗi frame (độ) | 0.5 |
| delay_ms | Delay mỗi frame (ms) | 10 |
| saturation | Độ bão hòa 0.0–1.0 | 1.0 |
| value | Độ sáng 0.0–1.0 | 1.0 |

---

## Nguyên lý hoạt động

```
DTS (my,pwm-rgb)
      ↓ kernel match compatible
 pwm_rgb.ko (platform_driver)
      ↓ devm_pwm_get("red/green/blue")
 EHRPWM hardware (ehrpwm1, ehrpwm2)
      ↓ tín hiệu PWM ra chân vật lý
 /sys/class/leds/rgb:red|green|blue/brightness
      ↓ userspace write 0–255
 pwm_rgb_set_brightness() → duty cycle
      ↓
 LED sáng theo màu mong muốn
```
