#ifndef __MQTT__
#define __MQTT__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define MQTT_ADDRESS     "tcp://broker.hivemq.com:1883"  // Thay bằng HiveMQ broker của bạn
#define MQTT_CLIENTID    "BBB_Client"
#define MQTT_QOS         1
#define MQTT_TIMEOUT     10000L
#define MQTT_RETAINED    1

typedef void (*MQTT_HandleMessage)(const char* topic, const char* payload);


int MQTT_Init(MQTT_HandleMessage handler);
int MQTT_Reconnect(void);
int MQTT_IsConnected(void);
int MQTT_SubscribeTopic(const char* topic);
int MQTT_PublishMessage(const char* topic, const char* payload);
void MQTT_Disconnect(void);

#endif