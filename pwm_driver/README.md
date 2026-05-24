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
    ├── pwm_rgb/                    ← Kernel module driver
    │   ├── src/
    │   │   ├── pwm_rgb.c
    │   │   └── Makefile
    │   ├── Config.in
    │   └── pwm_rgb.mk
    │
    ├── rgb/                        ← Userspace library
    │   ├── src/
    │   │   ├── pwm/
    │   │   │   ├── pwm.h
    │   │   │   └── pwm.c
    │   │   └── rgb_led/
    │   │       ├── rgb_led.h
    │   │       └── rgb_led.c
    │   ├── Config.in
    │   └── rgb.mk
    │
    └── pwm_test/                   ← Userspace test app
        ├── src/
        │   └── pwm_test.c
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
        AM33XX_PADCONF(AM335X_PIN_GPMC_A2, PIN_OUTPUT, MUX_MODE6)   /* P9_14 ehrpwm1A */
        AM33XX_PADCONF(AM335X_PIN_GPMC_A3, PIN_OUTPUT, MUX_MODE6)   /* P9_16 ehrpwm1B */
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
        └── [*] EHRPWM PWM support     (CONFIG_PWM_TIEHRPWM)
        └── [*] ECAP PWM support       (CONFIG_PWM_ECAP)

Device Drivers
  └── LED Support
        └── [*] LED Class Support      (CONFIG_LEDS_CLASS)
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
    struct pwm_rgb_led *led = container_of(cdev, struct pwm_rgb_led, cdev); // Có địa chỉ của một member trong struct --> lấy địa chỉ của struct chứa nó
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

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL); // Cấp phát bộ nhớ cho struct pwm_rgb_priv, tự động giải phóng khi device (dev) bị remove
    if (!priv)
        return -ENOMEM;

    for (i = 0; i < 3; i++) {
        /* Lấy PWM theo tên khai báo trong DTS: pwm-names = "red","green","blue" */
        priv->leds[i].pwm = devm_pwm_get(dev, pwm_names[i]); // Lấy handle PWM từ DTS theo tên "red", "green", "blue"
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

        ret = led_classdev_register(dev, &priv->leds[i].cdev); // Đăng ký LED class device, tạo entry trong /sys/class/leds/
        if (ret) {
            dev_err(dev, "Failed to register LED '%s': %d\n", names[i], ret);
            return ret;
        }

        dev_info(dev, "Registered LED: %s\n", names[i]);
    }

    platform_set_drvdata(pdev, priv); // Lưu pointer đến struct pwm_rgb_priv trong platform device để dùng khi remove 
    dev_info(dev, "PWM RGB driver probed OK\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* remove: kernel >= 6.x đổi sang void, không return int              */
/* ------------------------------------------------------------------ */
static void pwm_rgb_remove(struct platform_device *pdev)
{
    struct pwm_rgb_priv *priv = platform_get_drvdata(pdev); // Lấy pointer đến struct pwm_rgb_priv đã lưu trong probe
    struct pwm_state state;
    int i;

    for (i = 0; i < 3; i++) {
	led_classdev_unregister(&priv->leds[i].cdev);           // Hủy đăng ký LED class device, xóa entry trong /sys/class/leds/
        pwm_get_state(priv->leds[i].pwm, &state);           // Lấy trạng thái hiện tại của PWM
        state.enabled = false;                              // Tắt PWM khi driver bị remove
        pwm_apply_might_sleep(priv->leds[i].pwm, &state);   // Áp dụng trạng thái mới cho PWM, đảm bảo tắt LED khi driver bị remove
    }
}

/* ------------------------------------------------------------------ */
/* DTS match table — phải khớp với compatible trong .dts               */
/* ------------------------------------------------------------------ */
static const struct of_device_id pwm_rgb_of_match[] = {
    { .compatible = "my,pwm-rgb" },
    { /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, pwm_rgb_of_match); // Đăng ký bảng match với kernel để hỗ trợ autoload driver khi phát hiện node compatible trong DTS


/* Platform driver structure */
static struct platform_driver pwm_rgb_driver = {
    .probe  = pwm_rgb_probe,
    .remove = pwm_rgb_remove,
    .driver = {
        .name           = "pwm-rgb",
        .of_match_table = pwm_rgb_of_match,
    },
};

module_platform_driver(pwm_rgb_driver); // Macro đăng ký platform driver, tự động tạo init/exit functions

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

## Bước 4 — Thư viện RGB (`rgb`)

Thư viện userspace cung cấp abstraction cho PWM và chuyển đổi màu HSV → RGB. Scale duty cycle `0..1000` thay vì `0..255` để độ phân giải cao hơn.

### Cấu trúc

```
pwm/
  pwm.h / pwm.c     — Mở sysfs fd, ghi brightness, scale 0..1000 → 0..255
rgb_led/
  rgb_led.h         — Struct RGB_Led, macro màu định sẵn, khai báo hàm
  rgb_led.c         — RGBLed_Init/Show/Off, HSVtoRGB
```

### `package/rgb/src/rgb_led/rgb_led.h`

```c
#ifndef __RGB_LED__
#define __RGB_LED__

#include "pwm.h"

#define RED_PWM_CHANNEL   1
#define GREEN_PWM_CHANNEL 2
#define BLUE_PWM_CHANNEL  3

typedef struct {
    uint16_t red_value;
    uint16_t green_value;
    uint16_t blue_value;
} RGB_Led;

void    RGBLed_Init(void);
void    RGBLed_Show(RGB_Led rgb_led);
void    RGBLed_Off(void);

RGB_Led HSVtoRGB(float h, int s, int v);

/* Một số màu định sẵn (scale 0..PWM_MAX_DUTY) */
#define RGB_RED     (RGB_Led){ PWM_MAX_DUTY, 0,              0            }
#define RGB_GREEN   (RGB_Led){ 0,            PWM_MAX_DUTY,   0            }
#define RGB_BLUE    (RGB_Led){ 0,            0,              PWM_MAX_DUTY }
#define RGB_YELLOW  (RGB_Led){ PWM_MAX_DUTY, PWM_MAX_DUTY,   0            }
#define RGB_CYAN    (RGB_Led){ 0,            PWM_MAX_DUTY,   PWM_MAX_DUTY }
#define RGB_MAGENTA (RGB_Led){ PWM_MAX_DUTY, 0,              PWM_MAX_DUTY }
#define RGB_WHITE   (RGB_Led){ PWM_MAX_DUTY, PWM_MAX_DUTY,   PWM_MAX_DUTY }
#define RGB_OFF     (RGB_Led){ 0,            0,              0            }

#endif /* __RGB_LED__ */

```

### `package/rgb/src/rgb_led/rgb_led.c`

```c
#include "rgb_led.h"

/* ------------------------------------------------------------------ */
/* Khởi tạo 3 kênh PWM                                                */
/* ------------------------------------------------------------------ */
void RGBLed_Init(void)
{
    PWM_Config(RED_PWM_CHANNEL);
    PWM_Config(GREEN_PWM_CHANNEL);
    PWM_Config(BLUE_PWM_CHANNEL);
}

/* ------------------------------------------------------------------ */
/* Hiển thị màu                                                        */
/* ------------------------------------------------------------------ */
void RGBLed_Show(RGB_Led rgb_led)
{
    PWM_SetDuty(RED_PWM_CHANNEL,   rgb_led.red_value);
    PWM_SetDuty(GREEN_PWM_CHANNEL, rgb_led.green_value);
    PWM_SetDuty(BLUE_PWM_CHANNEL,  rgb_led.blue_value);
}

/* ------------------------------------------------------------------ */
/* Tắt LED                                                             */
/* ------------------------------------------------------------------ */
void RGBLed_Off(void)
{
    RGBLed_Show(RGB_OFF);
}

/* ------------------------------------------------------------------ */
/* Chuyển đổi HSV → RGB                                               */
/* h: 0.0 ~ 360.0                                                      */
/* s: 0 ~ PWM_MAX_DUTY (1000)                                         */
/* v: 0 ~ PWM_MAX_DUTY (1000)                                         */
/* ------------------------------------------------------------------ */
RGB_Led HSVtoRGB(float h, int s, int v)
{
    RGB_Led rgb;
    float   hh, p, q, t, ff;
    int     i;
    float   S = s / (float)PWM_MAX_DUTY;
    float   V = v / (float)PWM_MAX_DUTY;

    if (s <= 0) {
        rgb.red_value = rgb.green_value = rgb.blue_value = (uint16_t)v;
        return rgb;
    }

    if (h >= 360.0f) h = 0.0f;

    hh = h / 60.0f;
    i  = (int)hh;
    ff = hh - i;

    p = V * (1.0f - S);
    q = V * (1.0f - (S * ff));
    t = V * (1.0f - (S * (1.0f - ff)));

    switch (i) {
        case 0:
            rgb.red_value   = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(t * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(p * PWM_MAX_DUTY);
            break;
        case 1:
            rgb.red_value   = (uint16_t)(q * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(p * PWM_MAX_DUTY);
            break;
        case 2:
            rgb.red_value   = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(t * PWM_MAX_DUTY);
            break;
        case 3:
            rgb.red_value   = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(q * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(V * PWM_MAX_DUTY);
            break;
        case 4:
            rgb.red_value   = (uint16_t)(t * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(V * PWM_MAX_DUTY);
            break;
        default:
            rgb.red_value   = (uint16_t)(V * PWM_MAX_DUTY);
            rgb.green_value = (uint16_t)(p * PWM_MAX_DUTY);
            rgb.blue_value  = (uint16_t)(q * PWM_MAX_DUTY);
            break;
    }

    return rgb;
}

```

### `package/rgb/src/pwm/pwm.h`

```c
#ifndef __PWM__
#define __PWM__

#include <stdlib.h>
#include <stdint.h>

#define PWM_MAX_DUTY 1000

/* Sysfs paths cho từng channel */
#define PWM_CH1_PATH  "/sys/class/leds/rgb:red/brightness"
#define PWM_CH2_PATH  "/sys/class/leds/rgb:green/brightness"
#define PWM_CH3_PATH  "/sys/class/leds/rgb:blue/brightness"

void     PWM_Config(int channel);
void     PWM_SetDuty(int channel, uint16_t duty);

#endif /* __PWM__ */

```


### `package/rgb/src/pwm/pwm.c`

```c
#include "pwm.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* File descriptors cho 3 channel, mở 1 lần */
static int pwm_fd[4] = { -1, -1, -1, -1 }; /* index 1..3 */

static const char *pwm_path(int channel)
{
    switch (channel) {
        case 1: return PWM_CH1_PATH;
        case 2: return PWM_CH2_PATH;
        case 3: return PWM_CH3_PATH;
        default: return NULL;
    }
}

/* Mở file sysfs, sẵn sàng ghi */
void PWM_Config(int channel)
{
    const char *path = pwm_path(channel);
    if (!path) {
        fprintf(stderr, "PWM_Config: invalid channel %d\n", channel);
        return;
    }

    if (pwm_fd[channel] >= 0)
        return; /* đã mở rồi */

    pwm_fd[channel] = open(path, O_WRONLY);
    if (pwm_fd[channel] < 0)
        perror(path);
}

/* Ghi duty cycle vào sysfs
 * duty: 0 ~ PWM_MAX_DUTY (1000) → map sang 0 ~ 255 */
void PWM_SetDuty(int channel, uint16_t duty)
{
    char buf[8];
    int  len;
    int  brightness;

    if (channel < 1 || channel > 3 || pwm_fd[channel] < 0)
        return;

    if (duty > PWM_MAX_DUTY)
        duty = PWM_MAX_DUTY;

    /* Scale: 0..1000 → 0..255 */
    brightness = (int)((uint32_t)duty * 255 / PWM_MAX_DUTY);

    len = snprintf(buf, sizeof(buf), "%d", brightness);
    lseek(pwm_fd[channel], 0, SEEK_SET);
    (void)write(pwm_fd[channel], buf, len);
}

```


### `package/rgb/Config.in`

```
config BR2_PACKAGE_RGB
	bool "rgb"
	help
	  RGB LED library for BeagleBone Black.
	  Installs librgb.a và librgb.so.
```

### `package/rgb/rgb.mk`

```makefile
RGB_VERSION     = 1.0
RGB_SITE        = $(TOPDIR)/package/rgb/src
RGB_SITE_METHOD = local
RGB_INSTALL_STAGING = YES

define RGB_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC \
		-I$(@D)/pwm -I$(@D)/rgb_led \
		-c $(@D)/pwm/pwm.c -o $(@D)/pwm.o

	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC \
		-I$(@D)/pwm -I$(@D)/rgb_led \
		-c $(@D)/rgb_led/rgb_led.c -o $(@D)/rgb_led.o

	$(TARGET_AR) rcs $(@D)/librgb.a $(@D)/pwm.o $(@D)/rgb_led.o
	$(TARGET_CC) -shared -o $(@D)/librgb.so $(@D)/pwm.o $(@D)/rgb_led.o
endef

define RGB_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/librgb.a       $(STAGING_DIR)/usr/lib/librgb.a
	$(INSTALL) -D -m 0755 $(@D)/librgb.so      $(STAGING_DIR)/usr/lib/librgb.so
	$(INSTALL) -D -m 0644 $(@D)/pwm/pwm.h      $(STAGING_DIR)/usr/include/pwm.h
	$(INSTALL) -D -m 0644 $(@D)/rgb_led/rgb_led.h $(STAGING_DIR)/usr/include/rgb_led.h
endef

define RGB_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/librgb.so $(TARGET_DIR)/usr/lib/librgb.so
endef

$(eval $(generic-package))
```

> **Lưu ý:** `rgb_led.h` phải dùng `#include "pwm.h"` (không phải `../pwm/pwm.h`) vì sau khi install vào staging cả 2 header cùng cấp trong `usr/include/`.

---

## Bước 5 — Userspace App (`pwm_test`)

App dùng thư viện `librgb` để điều khiển LED.

### `package/pwm_test/src/pwm_test.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <rgb_led.h>

static volatile int running = 1;

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Demo: chạy qua các màu cơ bản                                       */
/* ------------------------------------------------------------------ */
static void demo_colors(void)
{
    printf("=== Demo colors ===\n");

    printf("RED\n");     RGBLed_Show(RGB_RED);     sleep(1);
    printf("GREEN\n");   RGBLed_Show(RGB_GREEN);   sleep(1);
    printf("BLUE\n");    RGBLed_Show(RGB_BLUE);    sleep(1);
    printf("YELLOW\n");  RGBLed_Show(RGB_YELLOW);  sleep(1);
    printf("CYAN\n");    RGBLed_Show(RGB_CYAN);    sleep(1);
    printf("MAGENTA\n"); RGBLed_Show(RGB_MAGENTA); sleep(1);
    printf("WHITE\n");   RGBLed_Show(RGB_WHITE);   sleep(1);
    printf("OFF\n");     RGBLed_Off();
}

/* ------------------------------------------------------------------ */
/* Demo: fade in/out RED                                               */
/* ------------------------------------------------------------------ */
static void demo_fade(void)
{
    int i;
    printf("=== Fade RED ===\n");

    for (i = 0; i <= PWM_MAX_DUTY; i += 10) {
        RGBLed_Show((RGB_Led){ i, 0, 0 });
        usleep(10000);
    }
    for (i = PWM_MAX_DUTY; i >= 0; i -= 10) {
        RGBLed_Show((RGB_Led){ i, 0, 0 });
        usleep(10000);
    }
    RGBLed_Off();
}

/* ------------------------------------------------------------------ */
/* Rainbow — xoay hue liên tục bằng HSVtoRGB                          */
/* ------------------------------------------------------------------ */
static void rainbow(float step_deg, int delay_us, int sat, int val)
{
    float hue = 0.0f;

    printf("Rainbow running... (Ctrl+C to stop)\n");
    printf("Step=%.3f deg  Delay=%d us  S=%d  V=%d\n",
           step_deg, delay_us, sat, val);

    signal(SIGINT,  handle_sigint);
    signal(SIGTERM, handle_sigint);

    while (running) {
        RGBLed_Show(HSVtoRGB(hue, sat, val));

        hue += step_deg;
        if (hue >= 360.0f)
            hue -= 360.0f;

        usleep(delay_us);
    }

    RGBLed_Off();
    printf("\nStopped.\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s demo                        - chay qua 7 mau co ban\n", prog);
    printf("  %s fade                        - fade in/out RED\n", prog);
    printf("  %s set R G B                   - dat mau (0-%d)\n", prog, PWM_MAX_DUTY);
    printf("  %s off                         - tat het\n", prog);
    printf("  %s rainbow [step] [delay_ms] [sat] [val]\n", prog);
    printf("       step    : buoc hue moi frame (default 0.5 deg)\n");
    printf("       delay_ms: delay moi frame    (default 10 ms)\n");
    printf("       sat     : saturation 0-%d   (default %d)\n", PWM_MAX_DUTY, PWM_MAX_DUTY);
    printf("       val     : brightness 0-%d   (default %d)\n", PWM_MAX_DUTY, PWM_MAX_DUTY);
    printf("\nVi du:\n");
    printf("  %s rainbow\n", prog);
    printf("  %s rainbow 0.1 5\n", prog);
    printf("  %s rainbow 1.0 5 1000 500\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    RGBLed_Init();

    if (strcmp(argv[1], "demo") == 0) {
        demo_colors();

    } else if (strcmp(argv[1], "fade") == 0) {
        demo_fade();

    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s set R G B\n", argv[0]);
            return 1;
        }
        RGB_Led c = {
            .red_value   = atoi(argv[2]),
            .green_value = atoi(argv[3]),
            .blue_value  = atoi(argv[4]),
        };
        printf("Set RGB(%d, %d, %d)\n", c.red_value, c.green_value, c.blue_value);
        RGBLed_Show(c);

    } else if (strcmp(argv[1], "off") == 0) {
        RGBLed_Off();
        printf("OFF\n");

    } else if (strcmp(argv[1], "rainbow") == 0) {
        float step  = argc >= 3 ? atof(argv[2]) : 0.5f;
        int   delay = argc >= 4 ? atoi(argv[3]) * 1000 : 10000;
        int   sat   = argc >= 5 ? atoi(argv[4]) : PWM_MAX_DUTY;
        int   val   = argc >= 6 ? atoi(argv[5]) : PWM_MAX_DUTY;

        if (step < 0.01f)        step = 0.01f;
        if (sat  < 0)            sat  = 0;
        if (sat  > PWM_MAX_DUTY) sat  = PWM_MAX_DUTY;
        if (val  < 0)            val  = 0;
        if (val  > PWM_MAX_DUTY) val  = PWM_MAX_DUTY;

        rainbow(step, delay, sat, val);

    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

```


### `package/pwm_test/Config.in`

```
config BR2_PACKAGE_PWM_TEST
	bool "pwm_test"
	select BR2_PACKAGE_RGB
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
		-I$(STAGING_DIR)/usr/include \
		$(@D)/pwm_test.c \
		-L$(STAGING_DIR)/usr/lib \
		-lrgb -lm \
		-o $(@D)/pwm_test
endef

define PWM_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pwm_test $(TARGET_DIR)/usr/bin/pwm_test
endef

$(eval $(generic-package))
```

---

## Bước 6 — Đăng ký package với Buildroot

Thêm vào `package/Config.in`:

```
source "package/pwm_rgb/Config.in"
source "package/rgb/Config.in"
source "package/pwm_test/Config.in"
```

Bật trong menuconfig:

```bash
make menuconfig
# Target packages → pwm_rgb  → [*]
# Target packages → rgb       → [*]
# Target packages → pwm_test  → [*]
```

---

## Bước 7 — Build

```bash
# Build kernel + DTB
make linux-rebuild

# Build packages theo thứ tự
make pwm_rgb-rebuild
make rgb-rebuild
make pwm_test-rebuild

# Pack image
make
```

---

## Bước 8 — Deploy lên board

### Thiết lập network USB RNDIS

```bash
sudo ip addr add 192.168.7.1/24 dev <interface>
sudo ip link set <interface> up
ping 192.168.7.2   # kiểm tra kết nối
```

### Copy file lên board

```bash
scp output/build/pwm_rgb-1.0/pwm_rgb.ko  root@192.168.7.2:/lib/modules/
scp output/target/usr/lib/librgb.so       root@192.168.7.2:/usr/lib/
scp output/target/usr/bin/pwm_test        root@192.168.7.2:/usr/bin/
```

---

## Bước 9 — Test trên board

### Load driver

```bash
insmod /lib/modules/pwm_rgb.ko

# Kiểm tra probe thành công
dmesg | grep pwm
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
echo 255 > /sys/class/leds/rgb:red/brightness
echo 255 > /sys/class/leds/rgb:green/brightness
echo 255 > /sys/class/leds/rgb:blue/brightness
echo 0   > /sys/class/leds/rgb:red/brightness
```

### Test bằng pwm_test

```bash
pwm_test demo                      # chạy qua 7 màu cơ bản
pwm_test fade                      # fade in/out RED
pwm_test set 1000 0 0              # đỏ full
pwm_test set 1000 500 0            # màu cam
pwm_test off                       # tắt hết

# Rainbow — chuyển màu cầu vồng liên tục
pwm_test rainbow                   # mặc định: mịn, vừa phải
pwm_test rainbow 0.1 5             # rất mịn, nhanh
pwm_test rainbow 1.0 5 1000 500    # nhanh, độ sáng 50%
pwm_test rainbow 0.3 15 800 1000   # màu pastel
# Ctrl+C để dừng
```

### Tham số rainbow

| Tham số | Ý nghĩa | Mặc định |
|---------|---------|---------|
| step | Bước hue mỗi frame (độ) | 0.5 |
| delay_ms | Delay mỗi frame (ms) | 10 |
| sat | Saturation 0–1000 | 1000 |
| val | Brightness 0–1000 | 1000 |

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
      ↓ userspace ghi 0–255
 pwm_rgb_set_brightness() → duty cycle
      ↓
 librgb (PWM_SetDuty scale 0..1000 → 0..255)
      ↓
 pwm_test (RGBLed_Show / HSVtoRGB)
      ↓
 LED sáng theo màu mong muốn
```
