#include "mqtt.h"

static volatile MQTTClient_deliveryToken deliveredtoken;
static MQTTClient client = NULL;
static MQTT_HandleMessage message_handler = NULL;
static volatile int mqtt_connected = 0;
static int mqtt_client_created = 0;

static int MQTT_ConnectInternal(void)
{
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  int rc;

  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;

  rc = MQTTClient_connect(client, &conn_opts);
  if (rc != MQTTCLIENT_SUCCESS)
  {
    mqtt_connected = 0;
    printf("Failed to connect, return code %d\n", rc);
    return rc;
  }

  mqtt_connected = 1;
  printf("MQTT connected successfully\n");
  return MQTTCLIENT_SUCCESS;
}

void delivered(void *context, MQTTClient_deliveryToken dt) 
{
  printf("Message with token value %d delivered\n", dt);
  deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) 
{
  char payload[256] = {0};
  char topic[128] = {0};

  // Safe copy payload
  int len = message->payloadlen;
  if (len >= sizeof(payload))
    len = sizeof(payload) - 1;

  memcpy(payload, message->payload, len);
  payload[len] = '\0';

  // Safe copy topic
  int tlen = (topicLen > 0) ? topicLen : (int)strlen(topicName);
  if (tlen >= sizeof(topic))
    tlen = sizeof(topic) - 1;

  memcpy(topic, topicName, tlen);
  topic[tlen] = '\0';

  printf("Message arrived\nTopic: %s\nMessage: %s\n", topic, payload);

  if (message_handler) 
  {
    message_handler(topic, payload);
  }

  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);

  return 1;
}

void connlost(void *context, char *cause) 
{
  mqtt_connected = 0;
  printf("Connection lost: %s\n", (cause != NULL) ? cause : "unknown");
}

int MQTT_IsConnected(void)
{
  if (!mqtt_client_created)
    return 0;

  if (!mqtt_connected)
    return 0;

  if (MQTTClient_isConnected(client) == 0)
  {
    mqtt_connected = 0;
    return 0;
  }

  return 1;
}

int MQTT_Init(MQTT_HandleMessage handler) 
{
  int rc;

  message_handler = handler;

  if (!mqtt_client_created)
  {
    rc = MQTTClient_create(&client, MQTT_ADDRESS, MQTT_CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS)
    {
      printf("Failed to create MQTT client, return code %d\n", rc);
      return rc;
    }

    rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
    if (rc != MQTTCLIENT_SUCCESS)
    {
      printf("Failed to set MQTT callbacks, return code %d\n", rc);
      MQTTClient_destroy(&client);
      return rc;
    }

    mqtt_client_created = 1;
  }

  if (MQTT_IsConnected())
  {
    return MQTTCLIENT_SUCCESS;
  }

  return MQTT_ConnectInternal();
}

int MQTT_Reconnect(void)
{
  if (!mqtt_client_created)
  {
    if (message_handler == NULL)
    {
      return MQTTCLIENT_FAILURE;
    }

    return MQTT_Init(message_handler);
  }

  if (MQTT_IsConnected())
  {
    return MQTTCLIENT_SUCCESS;
  }

  return MQTT_ConnectInternal();
}

int MQTT_SubscribeTopic(const char* topic)
{
  if (!MQTT_IsConnected())
  {
    return MQTTCLIENT_DISCONNECTED;
  }

  printf("Subscribing to topic %s\nfor client %s using QoS %d\n\n", topic, MQTT_CLIENTID, MQTT_QOS);
  int rc = MQTTClient_subscribe(client, topic, MQTT_QOS);
  if (rc != MQTTCLIENT_SUCCESS)  
  {
    printf("Failed to subscribe, return code %d\n", rc);
  }
  return rc;
}

int MQTT_PublishMessage(const char* topic, const char* payload)
{
  int rc;

  if (!MQTT_IsConnected())
  {
    return MQTTCLIENT_DISCONNECTED;
  }

  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;
  pubmsg.payload = (char*)payload;
  pubmsg.payloadlen = strlen(payload);
  pubmsg.qos = MQTT_QOS;
  pubmsg.retained = MQTT_RETAINED;
  printf("Publishing message: %s\n", payload);
  rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
  if (rc != MQTTCLIENT_SUCCESS) {
      if (rc == MQTTCLIENT_DISCONNECTED)
      {
        mqtt_connected = 0;
      }
      printf("Failed to publish, return code %d\n", rc);
      return rc;
  }
  MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
  printf("Message with delivery token %d delivered\n", token);
  return rc;
}



void MQTT_Disconnect(void)
{
  if (!mqtt_client_created)
  {
    return;
  }

  if (MQTT_IsConnected())
  {
    MQTTClient_disconnect(client, 10000);
  }

  MQTTClient_destroy(&client);
  mqtt_connected = 0;
  mqtt_client_created = 0;
}