#ifndef __ALL_HEADER_H__
#define __ALL_HEADER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "oled.h"
#include "nrf24l01.h"
#include "rgb_led.h"

#if defined(TEST_STM32)

#include "stm32f10x.h"                  // Device header
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#else

#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

#include "mqtt.h"
#include <cjson/cJSON.h>

#define MQTT_TOPIC       "BeagleBone/RGB"

#endif

#endif // __ALL_HEADER_H__