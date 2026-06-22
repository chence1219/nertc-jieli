#include "app_nertc_call.h"

#include "app_config.h"
#include "system/includes.h"
#include "app_core.h"
#include "event/key_event.h"
#include "event/net_event.h"
#include "wifi/wifi_connect.h"
#include "audio_io.h"
#include "nertc_protocol.h"
#include "nertc_log.h"
#include "nertc_ota.h"
#include "wifi_app_task.h"
#include "music_player.h"

#include <stdio.h>
#include <string.h>

#define TAG "APPCALL"

typedef struct {
    app_nertc_call_state_t state;
    char device_id[64];
    char device_mac[32];
    bool net_connected;
    bool protocol_started;
    bool room_joined;
} app_nertc_call_context_t;

static app_nertc_call_context_t g_call;

const char *app_nertc_call_state_name(app_nertc_call_state_t state)
{
    switch (state) {
    case APP_NERTC_CALL_STATE_STARTING:
        return "STARTING";
    case APP_NERTC_CALL_STATE_IDLE:
        return "IDLE";
    case APP_NERTC_CALL_STATE_LISTENING:
        return "LISTENING";
    case APP_NERTC_CALL_STATE_SPEAKING:
        return "SPEAKING";
    default:
        return "UNKNOWN";
    }
}

static bool app_nertc_call_is_talk_key(const struct key_event *key)
{
    if (!key) {
        return false;
    }

    return key->value == KEY_ENC;
}

static bool app_nertc_call_is_stop_music_key(const struct key_event *key)
{
    if (!key) {
        return false;
    }

    return key->value == KEY_POWER;
}

static void app_nertc_call_set_state(app_nertc_call_state_t state)
{
    app_nertc_call_state_t old_state = g_call.state;

    if (old_state == state) {
        return;
    }

    g_call.state = state;
    NERTC_LOGI("state: %s -> %s",
               app_nertc_call_state_name(old_state),
               app_nertc_call_state_name(state));
}

static void app_nertc_call_fill_device_id(void)
{
    u8 mac[6] = {0};

    // if (wifi_get_mac(mac) == 0) {
    //     snprintf(g_call.device_mac, sizeof(g_call.device_mac),
    //              "%02X:%02X:%02X:%02X:%02X:%02X",
    //              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // } else {
    //     snprintf(g_call.device_mac, sizeof(g_call.device_mac), "%s",
    //              APP_DEFAULT_DEVICE_ID);
    // }

    snprintf(g_call.device_mac, sizeof(g_call.device_mac), "%s",
                APP_DEFAULT_DEVICE_ID);

    snprintf(g_call.device_id, sizeof(g_call.device_id), "%s", g_call.device_mac);
    NERTC_LOGI("device_id=%s", g_call.device_id);
}

static bool app_nertc_call_is_audio_channel_opened(void)
{
    return g_call.protocol_started && nertc_protocol_is_audio_channel_opened();
}

static void app_nertc_call_on_audio(const nertc_protocol_audio_data_t *audio, void *user_data)
{
    (void)user_data;

    if (!audio || !audio->data || audio->length <= 0) {
        return;
    }

    if (g_call.state != APP_NERTC_CALL_STATE_SPEAKING) {
        return;
    }

    audio_io_push_downlink(audio->data, audio->length);
}

static void app_nertc_call_on_protocol_event(const nertc_protocol_event_t *event, void *user_data)
{
    (void)user_data;

    if (!event) {
        return;
    }

    switch (event->event) {
    case NERTC_PROTOCOL_EVENT_CONNECTED:
        g_call.room_joined = true;
        audio_io_init(event->data.connected.sample_rate > 0
                          ? event->data.connected.sample_rate
                          : NERTC_DEFAULT_SAMPLE_RATE,
                      NERTC_DEFAULT_CHANNELS);
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_IDLE);
        break;

    case NERTC_PROTOCOL_EVENT_DISCONNECTED:
        g_call.room_joined = false;
        audio_io_stop();
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_STARTING);
        break;

    case NERTC_PROTOCOL_EVENT_AI_READY:
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_LISTENING);
        break;

    case NERTC_PROTOCOL_EVENT_AI_DATA:
        if (!app_nertc_call_is_audio_channel_opened() &&
            g_call.state != APP_NERTC_CALL_STATE_IDLE &&
            g_call.state != APP_NERTC_CALL_STATE_STARTING) {
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_IDLE);
        }
        break;

    case NERTC_PROTOCOL_EVENT_TTS_START:
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_SPEAKING);
        break;

    case NERTC_PROTOCOL_EVENT_TTS_STOP:
        if (app_nertc_call_is_audio_channel_opened()) {
            int err;

            err = nertc_protocol_manual_start_listen();
            if (err) {
                NERTC_LOGE("manual start listen failed: %d", err);
            }
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_LISTENING);
        } else {
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_IDLE);
        }
        break;

    case NERTC_PROTOCOL_EVENT_ERROR:
        NERTC_LOGE("protocol error code=%d msg=%s",
                   event->data.error.error_code,
                   event->data.error.error_msg ? event->data.error.error_msg : "");
        if (!g_call.room_joined && g_call.protocol_started) {
            nertc_protocol_deinit();
            g_call.protocol_started = false;
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_STARTING);
        }
        break;

    default:
        break;
    }
}

static int app_nertc_call_start_protocol(const nertc_ota_result_t *ota_result)
{
    nertc_protocol_config_t config;
    int err;

    if (g_call.protocol_started) {
        return 0;
    }

    app_nertc_call_fill_device_id();

    nertc_protocol_config_init(&config);
    config.app_key = APP_KEY;
    config.device_id = g_call.device_id;
    config.enable_mcp_server = true;
    config.enable_asr = true;
    if (ota_result) {
        config.mqtt_endpoint = ota_result->mqtt_endpoint;
        config.mqtt_client_id = ota_result->mqtt_client_id;
        config.mqtt_username = ota_result->mqtt_username;
        config.mqtt_password = ota_result->mqtt_password;
        config.mqtt_publish_topic = ota_result->mqtt_publish_topic;
    }

    err = nertc_protocol_init_with_config(&config);
    if (err) {
        NERTC_LOGE("nertc init failed: %d", err);
        return err;
    }

    nertc_protocol_set_event_callback(app_nertc_call_on_protocol_event, NULL);
    nertc_protocol_set_audio_callback(app_nertc_call_on_audio, NULL);

    err = nertc_protocol_start(APP_CNAME, APP_UID);
    if (err) {
        NERTC_LOGE("nertc start failed: %d", err);
        nertc_protocol_deinit();
        return err;
    }

    g_call.protocol_started = true;
    return 0;
}

static void app_nertc_call_close_audio_channel(void)
{
    if (app_nertc_call_is_audio_channel_opened()) {
        nertc_protocol_close_audio_channel();
    }
}

static void app_nertc_call_stop_protocol(void)
{
    app_nertc_call_close_audio_channel();
    audio_io_stop();

    if (g_call.protocol_started) {
        nertc_protocol_stop();
        nertc_protocol_deinit();
        g_call.protocol_started = false;
    }

    g_call.room_joined = false;
}

static int app_nertc_call_begin_talk(void)
{
    int err;

    if (!g_call.net_connected || !g_call.room_joined) {
        NERTC_LOGW("ignore talk start before net/nertc ready");
        return true;
    }

    switch (g_call.state) {
    case APP_NERTC_CALL_STATE_IDLE:
        if (music_player_is_playing()) {
            music_player_stop();
        }
        if (!audio_io_is_started()) {
            audio_io_start();
        }
        if (!app_nertc_call_is_audio_channel_opened()) {
            err = nertc_protocol_open_audio_channel();
            if (err) {
                NERTC_LOGE("open audio channel failed: %d", err);
                return true;
            }
        }
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_LISTENING);
        return true;

    case APP_NERTC_CALL_STATE_LISTENING:
        app_nertc_call_close_audio_channel();
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_IDLE);
        return true;

    case APP_NERTC_CALL_STATE_SPEAKING:
        err = nertc_protocol_manual_interrupt();
        if (err) {
            NERTC_LOGE("manual interrupt failed: %d", err);
        }
        if (app_nertc_call_is_audio_channel_opened()) {
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_LISTENING);
        } else {
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_IDLE);
        }
        return true;

    case APP_NERTC_CALL_STATE_STARTING:
    default:
        return true;
    }
}

static int app_nertc_call_stop_music(void)
{
    if (!music_player_is_playing()) {
        return false;
    }

    music_player_stop();
    return true;
}

static int app_nertc_call_key_event_handler(struct key_event *key)
{
    if (!key || key->action != KEY_EVENT_CLICK) {
        return false;
    }

    if (app_nertc_call_is_stop_music_key(key)) {
        return app_nertc_call_stop_music();
    }

    if (!app_nertc_call_is_talk_key(key)) {
        return false;
    }

    switch (key->action) {
    case KEY_EVENT_CLICK:
        return app_nertc_call_begin_talk();
    default:
        return false;
    }
}

static int app_nertc_call_net_event_handler(struct net_event *event)
{
    nertc_ota_result_t ota_result;
    int err;

    if (!event || ASCII_StrCmp(event->arg, "net", 4)) {
        return false;
    }

    switch (event->event) {
    case NET_EVENT_CONNECTED:
        NERTC_LOGI("NET_EVENT_CONNECTED");
        g_call.net_connected = true;
        if (g_call.protocol_started) {
            return true;
        }

        memset(&ota_result, 0, sizeof(ota_result));
        if (APP_WIFI_READY_DELAY_TICKS > 0) {
            os_time_dly(APP_WIFI_READY_DELAY_TICKS);
        }
        app_nertc_call_fill_device_id();
        err = nertc_ota_check(&ota_result,
                              APP_KEY,
                              g_call.device_id,
                              g_call.device_mac,
                              NERTC_OTA_APP_VERSION);
        if (err != 0 || !ota_result.valid) {
            NERTC_LOGE("ota check failed: err=%d valid=%d", err, ota_result.valid);
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_STARTING);
            return true;
        }

        err = app_nertc_call_start_protocol(&ota_result);
        if (err != 0) {
            app_nertc_call_set_state(APP_NERTC_CALL_STATE_STARTING);
        }
        return true;

    case NET_EVENT_DISCONNECTED:
        NERTC_LOGI("NET_EVENT_DISCONNECTED");
        g_call.net_connected = false;
        app_nertc_call_stop_protocol();
        app_nertc_call_set_state(APP_NERTC_CALL_STATE_STARTING);
        return true;

    case NET_EVENT_DISCONNECTED_AND_REQ_CONNECT:
        wifi_return_sta_mode();
        return true;

    case NET_CONNECT_TIMEOUT_NOT_FOUND_SSID:
    case NET_CONNECT_ASSOCIAT_FAIL:
        return true;

    default:
        return false;
    }
}

static int app_nertc_call_event_handler(struct application *app, struct sys_event *event)
{
    (void)app;

    switch (event->type) {
    case SYS_KEY_EVENT:
        return app_nertc_call_key_event_handler((struct key_event *)event->payload);
    case SYS_NET_EVENT:
        return app_nertc_call_net_event_handler((struct net_event *)event->payload);
    default:
        return false;
    }
}

static int app_nertc_call_state_machine(struct application *app, enum app_state state, struct intent *it)
{
    (void)app;

    switch (state) {
    case APP_STA_CREATE:
        memset(&g_call, 0, sizeof(g_call));
        g_call.state = APP_NERTC_CALL_STATE_STARTING;
        audio_io_init(NERTC_DEFAULT_SAMPLE_RATE, NERTC_DEFAULT_CHANNELS);
        break;

    case APP_STA_START:
        if (it && it->action == ACTION_NERTC_CALL_MAIN) {
            NERTC_LOGI("app_nertc_call start");
        }
        break;

    case APP_STA_DESTROY:
        app_nertc_call_stop_protocol();
        break;

    default:
        break;
    }

    return 0;
}

static const struct application_operation app_nertc_call_ops = {
    .state_machine = app_nertc_call_state_machine,
    .event_handler = app_nertc_call_event_handler,
};

REGISTER_APPLICATION(app_nertc_call) = {
    .name  = "app_nertc_call",
    .ops   = &app_nertc_call_ops,
    .state = APP_STA_DESTROY,
};
