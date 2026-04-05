#ifndef __ALL_HEADER_H__
#define __ALL_HEADER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "oled.h"
#include "nrf24l01.h"
#include "rgb_led.h"

#include "mqtt.h"

#define MQTT_TOPIC       "BeagleBone/RGB"

#endif // __ALL_HEADER_H__