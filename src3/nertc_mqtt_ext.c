#include "app_config.h"
#include "system/includes.h"
#include "nertc_log.h"
#include "nertc_mqtt_ext.h"
#include "mqtt/MQTTClient.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "NERTC_MQTT"
#define MQTT_CMD_TIMEOUT_MS   30000
#define MQTT_YIELD_TIMEOUT_MS 100
#define MQTT_KEEPALIVE_SEC    60
#define MQTT_SENDBUF_SIZE     2048
#define MQTT_READBUF_SIZE     8192
#define MQTT_MAX_CLIENTS      4

typedef struct {
    Client client;
    Network network;
    unsigned char sendbuf[MQTT_SENDBUF_SIZE];
    unsigned char readbuf[MQTT_READBUF_SIZE];
    mqtt_on_connected_func on_connected;
    mqtt_on_disconnected_func on_disconnected;
    mqtt_on_message_func on_message;
    mqtt_on_error_func on_error;
    int yield_thread_pid;
    volatile bool yield_thread_running;
    volatile bool yield_thread_exited;
    volatile bool yield_started;
    volatile bool connected;
    volatile bool destroyed;
    volatile bool network_connected;
    OS_MUTEX op_mutex;
    int *op_owner_pid;
    unsigned int op_lock_count;
} nertc_mqtt_ext_t;

static nertc_sdk_ext_net_handle_t g_ext_net_handle;
static bool g_initialized = false;
static nertc_mqtt_ext_t *g_mqtt_registry[MQTT_MAX_CLIENTS];
static nertc_mqtt_ext_t *g_active_mqtt = NULL;

static bool mqtt_lock_impl(nertc_mqtt_ext_t *mqtt)
{
    int *self_pid;

    if (!mqtt) {
        return false;
    }

    self_pid = get_cur_thread_pid();
    if (!self_pid) {
        return false;
    }

    if (mqtt->op_owner_pid == self_pid) {
        mqtt->op_lock_count++;
        return true;
    }

    if (os_mutex_pend(&mqtt->op_mutex, 0) != OS_NO_ERR) {
        NERTC_LOGE("Failed to pend mqtt mutex");
        return false;
    }

    mqtt->op_owner_pid = self_pid;
    mqtt->op_lock_count = 1;
    return true;
}

static void mqtt_unlock(nertc_mqtt_ext_t *mqtt)
{
    int *self_pid;

    if (!mqtt) {
        return;
    }

    self_pid = get_cur_thread_pid();
    if (!self_pid || mqtt->op_owner_pid != self_pid || mqtt->op_lock_count == 0) {
        return;
    }

    mqtt->op_lock_count--;
    if (mqtt->op_lock_count == 0) {
        mqtt->op_owner_pid = NULL;
        os_mutex_post(&mqtt->op_mutex);
    }
}

static int mqtt_registry_add(nertc_mqtt_ext_t *mqtt)
{
    int i;

    for (i = 0; i < MQTT_MAX_CLIENTS; i++) {
        if (!g_mqtt_registry[i]) {
            g_mqtt_registry[i] = mqtt;
            return i;
        }
    }
    return -1;
}

static void mqtt_registry_remove(nertc_mqtt_ext_t *mqtt)
{
    int i;

    for (i = 0; i < MQTT_MAX_CLIENTS; i++) {
        if (g_mqtt_registry[i] == mqtt) {
            g_mqtt_registry[i] = NULL;
            return;
        }
    }
}

static void mqtt_message_adapter(MessageData *data)
{
    nertc_mqtt_ext_t *mqtt = g_active_mqtt;
    char topic[256] = {0};
    static char payload[MQTT_READBUF_SIZE];
    int copy_len;

    if (!mqtt || mqtt->destroyed || !mqtt->on_message || !data || !data->topicName || !data->message) {
        return;
    }

    if (data->topicName->lenstring.len > 0 && data->topicName->lenstring.data) {
        copy_len = data->topicName->lenstring.len;
        if (copy_len >= (int)sizeof(topic)) {
            copy_len = sizeof(topic) - 1;
        }
        memcpy(topic, data->topicName->lenstring.data, copy_len);
    }

    memset(payload, 0, sizeof(payload));
    if (data->message->payloadlen > 0 && data->message->payload) {
        copy_len = data->message->payloadlen;
        if (copy_len >= (int)sizeof(payload)) {
            copy_len = sizeof(payload) - 1;
        }
        memcpy(payload, data->message->payload, copy_len);
    }

    mqtt->on_message((mqtt_handle)mqtt, topic, payload);
}

static void mqtt_yield_thread(void *arg)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)arg;
    int ret;

    while (mqtt->yield_thread_running) {
        g_active_mqtt = mqtt;
        ret = MQTTYield(&mqtt->client, MQTT_YIELD_TIMEOUT_MS);
        g_active_mqtt = NULL;

        if (ret != 0 && mqtt->yield_thread_running && !mqtt->client.isconnected && mqtt->connected) {
            mqtt->connected = false;
            if (mqtt->on_disconnected) {
                mqtt->on_disconnected((mqtt_handle)mqtt);
            }
        }
        os_time_dly(1);
    }

    mqtt->yield_thread_exited = true;
}

static int mqtt_start_yield_thread(nertc_mqtt_ext_t *mqtt)
{
    char name[32];
    int ret;

    if (mqtt->yield_thread_running) {
        return 0;
    }

    mqtt->yield_thread_running = true;
    mqtt->yield_thread_exited = false;
    snprintf(name, sizeof(name), "mqtt_yield_%p", mqtt);
    ret = thread_fork(name, 25, 8192, 0, &mqtt->yield_thread_pid, mqtt_yield_thread, mqtt);
    if (ret != OS_NO_ERR) {
        mqtt->yield_thread_running = false;
        mqtt->yield_thread_pid = 0;
        return -1;
    }
    mqtt->yield_started = true;
    return 0;
}

static void mqtt_stop_yield_thread(nertc_mqtt_ext_t *mqtt)
{
    int wait_count = 0;

    if (!mqtt->yield_thread_running) {
        return;
    }

    mqtt->yield_thread_running = false;
    while (!mqtt->yield_thread_exited && wait_count < 300) {
        os_time_dly(10);
        wait_count++;
    }
    if (!mqtt->yield_thread_exited && mqtt->yield_thread_pid) {
        thread_kill(&mqtt->yield_thread_pid, KILL_FORCE);
    }
    mqtt->yield_thread_pid = 0;
    mqtt->yield_started = false;
}

static mqtt_handle ext_mqtt_create(void)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)calloc(1, sizeof(nertc_mqtt_ext_t));

    if (!mqtt) {
        return NULL;
    }

    NewNetwork(&mqtt->network);
    MQTTClient(&mqtt->client, &mqtt->network, MQTT_CMD_TIMEOUT_MS,
               mqtt->sendbuf, MQTT_SENDBUF_SIZE, mqtt->readbuf, MQTT_READBUF_SIZE);
    if (os_mutex_create(&mqtt->op_mutex) != OS_NO_ERR) {
        free(mqtt);
        return NULL;
    }
    mqtt->client.defaultMessageHandler = mqtt_message_adapter;

    if (mqtt_registry_add(mqtt) < 0) {
        os_mutex_del(&mqtt->op_mutex, OS_DEL_ALWAYS);
        free(mqtt);
        return NULL;
    }

    return (mqtt_handle)mqtt;
}

static void ext_mqtt_destroy(mqtt_handle handle)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    bool locked;

    if (!mqtt) {
        return;
    }

    mqtt->destroyed = true;
    mqtt_stop_yield_thread(mqtt);
    locked = mqtt_lock_impl(mqtt);

    if (mqtt->client.isconnected) {
        MQTTDisconnect(&mqtt->client);
    }
    if (mqtt->network_connected) {
        mqtt->network.disconnect(&mqtt->network);
        mqtt->network_connected = false;
    }

    mqtt_registry_remove(mqtt);
    if (locked) {
        mqtt_unlock(mqtt);
    }
    os_mutex_del(&mqtt->op_mutex, OS_DEL_ALWAYS);
    free(mqtt);
}

static void ext_mqtt_set_on_connected(mqtt_handle handle, mqtt_on_connected_func callback)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    if (mqtt) {
        mqtt->on_connected = callback;
    }
}

static void ext_mqtt_set_on_disconnected(mqtt_handle handle, mqtt_on_disconnected_func callback)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    if (mqtt) {
        mqtt->on_disconnected = callback;
    }
}

static void ext_mqtt_set_on_message(mqtt_handle handle, mqtt_on_message_func callback)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    if (mqtt) {
        mqtt->on_message = callback;
    }
}

static void ext_mqtt_set_on_error(mqtt_handle handle, mqtt_on_error_func callback)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    if (mqtt) {
        mqtt->on_error = callback;
    }
}

static bool ext_mqtt_connect(mqtt_handle handle, const char *host, int port,
                             const char *client_id, const char *username,
                             const char *password)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;
    int ret;

    if (!mqtt || !host || !client_id || !mqtt_lock_impl(mqtt)) {
        return false;
    }

    SetNetworkRecvTimeout(&mqtt->network, 5000);
    if (port == 8883) {
        NetWorkSetTLS(&mqtt->network);
    }

    ret = ConnectNetwork(&mqtt->network, (char *)host, port);
    if (ret != 0) {
        mqtt->network_connected = false;
        if (mqtt->on_error) {
            mqtt->on_error(handle, ret, "TCP connect failed");
        }
        mqtt_unlock(mqtt);
        return false;
    }

    mqtt->network_connected = true;
    connect_data.willFlag = 0;
    connect_data.MQTTVersion = 4;
    connect_data.clientID.cstring = (char *)client_id;
    connect_data.username.cstring = (char *)(username ? username : "");
    connect_data.password.cstring = (char *)(password ? password : "");
    connect_data.keepAliveInterval = MQTT_KEEPALIVE_SEC;
    connect_data.cleansession = 1;

    ret = MQTTConnect(&mqtt->client, &connect_data);
    if (ret != 0) {
        mqtt->network.disconnect(&mqtt->network);
        mqtt->network_connected = false;
        if (mqtt->on_error) {
            mqtt->on_error(handle, ret, "MQTT connect failed");
        }
        mqtt_unlock(mqtt);
        return false;
    }

    mqtt->connected = true;
    SetNetworkSendTimeout(&mqtt->network, MQTT_YIELD_TIMEOUT_MS);
    SetNetworkRecvTimeout(&mqtt->network, MQTT_YIELD_TIMEOUT_MS);
    if (mqtt->on_connected) {
        mqtt->on_connected(handle);
    }
    mqtt_unlock(mqtt);
    return true;
}

static void ext_mqtt_disconnect(mqtt_handle handle)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    bool locked;

    if (!mqtt) {
        return;
    }

    mqtt_stop_yield_thread(mqtt);
    locked = mqtt_lock_impl(mqtt);
    if (mqtt->client.isconnected) {
        MQTTDisconnect(&mqtt->client);
    }
    if (mqtt->network_connected) {
        mqtt->network.disconnect(&mqtt->network);
        mqtt->network_connected = false;
    }
    mqtt->connected = false;
    if (locked) {
        mqtt_unlock(mqtt);
    }
}

static bool ext_mqtt_is_connected(mqtt_handle handle)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    return mqtt && mqtt->connected && mqtt->client.isconnected;
}

static bool ext_mqtt_publish(mqtt_handle handle, const char *topic, const char *payload, int qos)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    MQTTMessage message;
    int ret;

    if (!mqtt || !topic || !payload || !mqtt_lock_impl(mqtt) || !mqtt->client.isconnected) {
        return false;
    }

    memset(&message, 0, sizeof(message));
    message.qos = (qos == 1) ? QOS1 : ((qos == 2) ? QOS2 : QOS0);
    message.payload = (void *)payload;
    message.payloadlen = strlen(payload);
    ret = MQTTPublish(&mqtt->client, topic, &message);
    if (ret == 0 && !mqtt->yield_started) {
        ret = mqtt_start_yield_thread(mqtt);
    }
    mqtt_unlock(mqtt);
    return ret == 0;
}

static bool ext_mqtt_subscribe(mqtt_handle handle, const char *topic, int qos)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    int ret;

    if (!mqtt || !topic || !mqtt_lock_impl(mqtt) || !mqtt->client.isconnected) {
        return false;
    }

    ret = MQTTSubscribe(&mqtt->client, topic,
                        (qos == 2) ? QOS2 : ((qos == 1) ? QOS1 : QOS0),
                        mqtt_message_adapter);
    mqtt_unlock(mqtt);
    return ret == 0;
}

static bool ext_mqtt_unsubscribe(mqtt_handle handle, const char *topic)
{
    nertc_mqtt_ext_t *mqtt = (nertc_mqtt_ext_t *)handle;
    int ret;

    if (!mqtt || !topic || !mqtt_lock_impl(mqtt) || !mqtt->client.isconnected) {
        return false;
    }

    ret = MQTTUnsubscribe(&mqtt->client, topic);
    mqtt_unlock(mqtt);
    return ret == 0;
}

nertc_sdk_ext_net_handle_t *nertc_mqtt_ext_get_handle(void)
{
    if (!g_initialized) {
        nertc_mqtt_ext_init();
    }
    return &g_ext_net_handle;
}

void nertc_mqtt_ext_init(void)
{
    if (g_initialized) {
        return;
    }

    memset(&g_ext_net_handle, 0, sizeof(g_ext_net_handle));
    memset(g_mqtt_registry, 0, sizeof(g_mqtt_registry));
    g_active_mqtt = NULL;
    g_ext_net_handle.create_mqtt = ext_mqtt_create;
    g_ext_net_handle.destroy_mqtt = ext_mqtt_destroy;
    g_ext_net_handle.set_mqtt_on_connected = ext_mqtt_set_on_connected;
    g_ext_net_handle.set_mqtt_on_disconnected = ext_mqtt_set_on_disconnected;
    g_ext_net_handle.set_mqtt_on_message = ext_mqtt_set_on_message;
    g_ext_net_handle.set_mqtt_on_error = ext_mqtt_set_on_error;
    g_ext_net_handle.mqtt_connect = ext_mqtt_connect;
    g_ext_net_handle.mqtt_disconnect = ext_mqtt_disconnect;
    g_ext_net_handle.mqtt_is_connected = ext_mqtt_is_connected;
    g_ext_net_handle.mqtt_publish = ext_mqtt_publish;
    g_ext_net_handle.mqtt_subscribe = ext_mqtt_subscribe;
    g_ext_net_handle.mqtt_unsubscribe = ext_mqtt_unsubscribe;
    g_initialized = true;
}

void nertc_mqtt_ext_deinit(void)
{
    int i;

    if (!g_initialized) {
        return;
    }

    for (i = 0; i < MQTT_MAX_CLIENTS; i++) {
        if (g_mqtt_registry[i]) {
            ext_mqtt_destroy((mqtt_handle)g_mqtt_registry[i]);
            g_mqtt_registry[i] = NULL;
        }
    }
    memset(&g_ext_net_handle, 0, sizeof(g_ext_net_handle));
    g_active_mqtt = NULL;
    g_initialized = false;
}
