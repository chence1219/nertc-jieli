/**
 * @file nertc_protocol.c
 * @brief Gen3 phase-1 NERTC protocol layer rebuilt on the first-gen shape.
 */

#include "app_config.h"
#include "nertc_log.h"
#include "system/includes.h"
#include "nertc_sdk.h"
#include "nertc_protocol.h"
#include "nertc_mqtt_ext.h"
#include "mcp_server.h"
#include "music_player.h"
#include "audio_io.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "NERTC"
#define NERTC_JOIN_TIMEOUT_MS 10000
#define NERTC_VBR_HEADER_SIZE 8
#define NERTC_AUDIO_CACHE_SIZE (NERTC_VBR_HEADER_SIZE + 2048)
#define NERTC_DEFAULT_CUSTOM_CONFIG  "{\"test_mode\":true,\"asr\":true}"

typedef struct {
    nertc_sdk_engine_t engine;
    volatile bool initialized;
    volatile bool joined;
    volatile bool audio_channel_opened;
    volatile bool asr_enabled;
    OS_SEM join_sem;
    volatile int join_result;
    char cname[64];
    uint64_t cid;
    uint64_t uid;
    int server_sample_rate;
    int server_out_sample_rate;
    int server_frame_duration;
    int samples_per_channel;
    nertc_sdk_audio_config_t recommended_audio_config;
    nertc_protocol_config_t config;
    nertc_protocol_event_callback_t event_callback;
    void *event_user_data;
    nertc_protocol_audio_callback_t audio_callback;
    void *audio_user_data;
} nertc_protocol_context_t;

static nertc_protocol_context_t g_ctx = {0};

static void dispatch_event(nertc_protocol_event_e event_type)
{
    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = event_type;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void dispatch_error_event(int code, const char *msg)
{
    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = NERTC_PROTOCOL_EVENT_ERROR;
        evt.data.error.error_code = code;
        evt.data.error.error_msg = msg;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void dispatch_connected_event(uint64_t cid,
                                     uint64_t uid,
                                     const nertc_sdk_recommended_config_t *cfg)
{
    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = NERTC_PROTOCOL_EVENT_CONNECTED;
        evt.data.connected.cid = cid;
        evt.data.connected.uid = uid;
        if (cfg) {
            evt.data.connected.sample_rate = cfg->recommended_audio_config.sample_rate;
            evt.data.connected.out_sample_rate = cfg->recommended_audio_config.out_sample_rate;
            evt.data.connected.frame_duration = cfg->recommended_audio_config.frame_duration;
            evt.data.connected.samples_per_channel = cfg->recommended_audio_config.samples_per_channel;
        } else {
            evt.data.connected.sample_rate = g_ctx.server_sample_rate;
            evt.data.connected.out_sample_rate = g_ctx.server_out_sample_rate;
            evt.data.connected.frame_duration = g_ctx.server_frame_duration;
            evt.data.connected.samples_per_channel = g_ctx.samples_per_channel;
        }
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void dispatch_user_event(nertc_protocol_event_e event_type,
                                const nertc_sdk_user_info_t *user)
{
    if (g_ctx.event_callback && user) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = event_type;
        evt.data.user.uid = user->uid;
        evt.data.user.name = user->name;
        evt.data.user.user_type = user->type;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void dispatch_audio_data_event(uint64_t uid,
                                      nertc_sdk_audio_encoded_frame_t *frame,
                                      bool is_mute)
{
    static uint8_t vbr_buf[NERTC_AUDIO_CACHE_SIZE];

    if (g_ctx.audio_callback && frame && frame->data && frame->length > 0) {
        nertc_protocol_audio_data_t audio;
        uint32_t packet_len = frame->length;

        if (packet_len + NERTC_VBR_HEADER_SIZE > sizeof(vbr_buf)) {
            NERTC_LOGE("Audio frame too large: %u", packet_len);
            return;
        }

        vbr_buf[0] = (packet_len >> 24) & 0xFF;
        vbr_buf[1] = (packet_len >> 16) & 0xFF;
        vbr_buf[2] = (packet_len >> 8) & 0xFF;
        vbr_buf[3] = packet_len & 0xFF;
        vbr_buf[4] = 0;
        vbr_buf[5] = 0;
        vbr_buf[6] = 0;
        vbr_buf[7] = 0;
        memcpy(vbr_buf + NERTC_VBR_HEADER_SIZE, frame->data, packet_len);

        memset(&audio, 0, sizeof(audio));
        audio.uid = uid;
        audio.data = vbr_buf;
        audio.length = packet_len + NERTC_VBR_HEADER_SIZE;
        audio.timestamp_ms = frame->timestamp_ms;
        audio.encoded_timestamp = frame->encoded_timestamp;
        audio.sample_rate = g_ctx.recommended_audio_config.out_sample_rate > 0
            ? g_ctx.recommended_audio_config.out_sample_rate
            : NERTC_DEFAULT_OUT_SAMPLE_RATE;
        audio.frame_duration = g_ctx.server_frame_duration > 0
            ? g_ctx.server_frame_duration
            : NERTC_DEFAULT_FRAME_DURATION;
        audio.is_mute_packet = is_mute;
        g_ctx.audio_callback(&audio, g_ctx.audio_user_data);
    }
}

static bool nertc_ai_type_matches(const char *type_str, size_t type_len, const char *expected)
{
    size_t expected_len;

    if (!type_str || !expected) {
        return false;
    }

    expected_len = strlen(expected);
    return type_len >= expected_len && memcmp(type_str, expected, expected_len) == 0;
}

static bool nertc_ai_data_contains(const char *data_str, size_t data_len, const char *needle)
{
    size_t needle_len;
    size_t i;

    if (!data_str || !needle) {
        return false;
    }

    needle_len = strlen(needle);
    if (needle_len == 0 || data_len < needle_len) {
        return false;
    }

    for (i = 0; i + needle_len <= data_len; ++i) {
        if (memcmp(data_str + i, needle, needle_len) == 0) {
            return true;
        }
    }

    return false;
}

static char *nertc_copy_json_fragment(const char *src, size_t len)
{
    char *copy;

    if (!src || len == 0) {
        return NULL;
    }

    copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

static void nertc_handle_mcp_data(const char *data_str, size_t data_len)
{
    char *wrapper_text;
    cJSON *wrapper;
    cJSON *payload;
    char *payload_text;

    wrapper_text = nertc_copy_json_fragment(data_str, data_len);
    if (!wrapper_text) {
        return;
    }

    wrapper = cJSON_Parse(wrapper_text);
    if (!wrapper) {
        mcp_server_parse_message(wrapper_text);
        free(wrapper_text);
        return;
    }

    payload = cJSON_GetObjectItem(wrapper, "payload");
    if (!payload) {
        mcp_server_parse_message(wrapper_text);
        free(wrapper_text);
        NERTC_LOGE("mcp wrapper parse failed");
        cJSON_Delete(wrapper);
        return;
    }
    free(wrapper_text);

    payload_text = cJSON_PrintUnformatted(payload);
    cJSON_Delete(wrapper);
    if (!payload_text) {
        return;
    }

    mcp_server_parse_message(payload_text);
    cJSON_free(payload_text);
}

static void nertc_handle_song_list_data(const char *data_str, size_t data_len)
{
    char *json_text;
    int count;

    json_text = nertc_copy_json_fragment(data_str, data_len);
    if (!json_text) {
        return;
    }

    count = music_player_update_song_list(json_text);
    free(json_text);
    if (count > 0) {
        music_player_maybe_play_first();
    }
}

static void nertc_handle_song_search_data(const char *data_str, size_t data_len)
{
    char *outer_text;
    cJSON *outer;
    cJSON *message;
    char *inner_text;
    char *inner_copy;
    int count;

    outer_text = nertc_copy_json_fragment(data_str, data_len);
    if (!outer_text) {
        return;
    }

    outer = cJSON_Parse(outer_text);
    free(outer_text);
    if (!outer) {
        NERTC_LOGE("songSearch outer parse failed");
        return;
    }

    message = cJSON_GetObjectItem(outer, "message");
    if (!cJSON_IsString(message) || !message->valuestring) {
        cJSON_Delete(outer);
        return;
    }

    inner_text = message->valuestring;
    if (!inner_text) {
        cJSON_Delete(outer);
        return;
    }

    inner_copy = nertc_copy_json_fragment(inner_text, strlen(inner_text));
    cJSON_Delete(outer);
    if (!inner_copy) {
        return;
    }

    count = music_player_update_song_list(inner_copy);
    free(inner_copy);
    if (count > 0) {
        music_player_maybe_play_first();
    }
}

static void nertc_on_error(const nertc_sdk_callback_context_t *ctx,
                           nertc_sdk_error_code_e code,
                           const char *msg)
{
    (void)ctx;
    NERTC_LOGE("on_error: code=%d, msg=%s", code, msg ? msg : "null");
    dispatch_error_event((int)code, msg);
}

static void nertc_on_license_expire_warning(const nertc_sdk_callback_context_t *ctx,
                                            int remaining_days)
{
    (void)ctx;
    NERTC_LOGW("on_license_expire_warning: remaining_days=%d", remaining_days);
    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = NERTC_PROTOCOL_EVENT_LICENSE_WARNING;
        evt.data.license_remaining_days = remaining_days;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void nertc_on_channel_status_changed(const nertc_sdk_callback_context_t *ctx,
                                            nertc_sdk_channel_state_e status,
                                            const char *msg)
{
    (void)ctx;
    NERTC_LOGI("on_channel_status_changed: status=%d, msg=%s", status, msg ? msg : "null");
    dispatch_event(NERTC_PROTOCOL_EVENT_CHANNEL_CHANGED);
}

static void nertc_on_join(const nertc_sdk_callback_context_t *ctx,
                          uint64_t cid,
                          uint64_t uid,
                          nertc_sdk_error_code_e code,
                          uint64_t elapsed,
                          const nertc_sdk_recommended_config_t *recommended_config)
{
    (void)ctx;
    NERTC_LOGI("on_join: cid=%llu, uid=%llu, code=%d, elapsed=%llu",
               (unsigned long long)cid,
               (unsigned long long)uid,
               code,
               (unsigned long long)elapsed);

    if (code != NERTC_SDK_ERR_SUCCESS) {
        g_ctx.join_result = (int)code;
        os_sem_post(&g_ctx.join_sem);
        dispatch_error_event((int)code, "Join failed");
        return;
    }

    g_ctx.joined = true;
    g_ctx.cid = cid;
    g_ctx.uid = uid;
    g_ctx.server_sample_rate = NERTC_DEFAULT_SAMPLE_RATE;
    g_ctx.server_out_sample_rate = NERTC_DEFAULT_OUT_SAMPLE_RATE;
    g_ctx.server_frame_duration = NERTC_DEFAULT_FRAME_DURATION;
    g_ctx.samples_per_channel = 0;
    memset(&g_ctx.recommended_audio_config, 0, sizeof(g_ctx.recommended_audio_config));

    if (recommended_config) {
        g_ctx.server_sample_rate = recommended_config->recommended_audio_config.sample_rate;
        g_ctx.server_out_sample_rate = recommended_config->recommended_audio_config.out_sample_rate;
        g_ctx.server_frame_duration = recommended_config->recommended_audio_config.frame_duration;
        g_ctx.samples_per_channel = recommended_config->recommended_audio_config.samples_per_channel;
        g_ctx.recommended_audio_config = recommended_config->recommended_audio_config;
        NERTC_LOGI("Recommended audio: sample_rate=%d, out_sample_rate=%d, frame_duration=%d",
                   g_ctx.server_sample_rate,
                   g_ctx.server_out_sample_rate,
                   g_ctx.server_frame_duration);
    }

    g_ctx.join_result = 0;
    os_sem_post(&g_ctx.join_sem);
    dispatch_connected_event(cid, uid, recommended_config);
}

static void nertc_on_disconnect(const nertc_sdk_callback_context_t *ctx,
                                nertc_sdk_error_code_e code,
                                int reason)
{
    (void)ctx;
    NERTC_LOGI("on_disconnect: code=%d, reason=%d", code, reason);
    g_ctx.joined = false;
    g_ctx.audio_channel_opened = false;

    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = NERTC_PROTOCOL_EVENT_DISCONNECTED;
        evt.data.disconnected.error_code = code;
        evt.data.disconnected.reason = reason;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void nertc_on_user_joined(const nertc_sdk_callback_context_t *ctx,
                                 const nertc_sdk_user_info_t *user)
{
    (void)ctx;
    if (!user) {
        return;
    }

    NERTC_LOGI("User joined: uid=%llu", (unsigned long long)user->uid);
    dispatch_user_event(NERTC_PROTOCOL_EVENT_USER_JOINED, user);
}

static void nertc_on_user_left(const nertc_sdk_callback_context_t *ctx,
                               const nertc_sdk_user_info_t *user,
                               int reason)
{
    (void)ctx;
    (void)reason;
    if (!user) {
        return;
    }

    NERTC_LOGI("User left: uid=%llu, reason=%d", (unsigned long long)user->uid, reason);
    dispatch_user_event(NERTC_PROTOCOL_EVENT_USER_LEFT, user);
}

static void nertc_on_user_audio_start(const nertc_sdk_callback_context_t *ctx,
                                      uint64_t uid,
                                      nertc_sdk_media_stream_e stream_type)
{
    (void)ctx;
    NERTC_LOGI("User audio start: uid=%llu, stream_type=%d",
               (unsigned long long)uid,
               stream_type);
    dispatch_event(NERTC_PROTOCOL_EVENT_USER_AUDIO_START);
}

static void nertc_on_user_audio_mute(const nertc_sdk_callback_context_t *ctx,
                                     uint64_t uid,
                                     nertc_sdk_media_stream_e stream_type,
                                     bool mute)
{
    (void)ctx;
    (void)stream_type;
    NERTC_LOGI("User audio mute: uid=%llu, mute=%d", (unsigned long long)uid, mute);
}

static void nertc_on_user_audio_stop(const nertc_sdk_callback_context_t *ctx,
                                     uint64_t uid,
                                     nertc_sdk_media_stream_e stream_type)
{
    (void)ctx;
    NERTC_LOGI("User audio stop: uid=%llu, stream_type=%d",
               (unsigned long long)uid,
               stream_type);
    dispatch_event(NERTC_PROTOCOL_EVENT_USER_AUDIO_STOP);
}

static void nertc_on_asr_caption_state_changed(const nertc_sdk_callback_context_t *ctx,
                                               nertc_sdk_asr_caption_state_e state,
                                               nertc_sdk_error_code_e code,
                                               const char *msg)
{
    (void)ctx;
    (void)msg;
    NERTC_LOGI("ASR caption state changed: state=%d, code=%d", state, code);
}

static void nertc_on_asr_caption_result(const nertc_sdk_callback_context_t *ctx,
                                        nertc_sdk_asr_caption_result_t *results,
                                        int result_count)
{
    int i;

    (void)ctx;

    for (i = 0; i < result_count; i++) {
        NERTC_LOGI("ASR result[%d]: %s (is_local=%d, is_final=%d)",
                   i,
                   results[i].content ? results[i].content : "",
                   results[i].is_local_user,
                   results[i].is_final);

        if (g_ctx.event_callback) {
            nertc_protocol_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.event = NERTC_PROTOCOL_EVENT_ASR_RESULT;
            evt.data.asr.user_id = results[i].user_id;
            evt.data.asr.is_local_user = results[i].is_local_user;
            evt.data.asr.timestamp = results[i].timestamp;
            evt.data.asr.content = results[i].content;
            evt.data.asr.is_final = results[i].is_final;
            g_ctx.event_callback(&evt, g_ctx.event_user_data);
        }
    }
}

static void nertc_on_ai_data(const nertc_sdk_callback_context_t *ctx,
                             nertc_sdk_ai_data_result_t *ai_data)
{
    const char *type_str;
    size_t type_len;
    const char *data_str;
    size_t data_len;
    bool is_mcp;

    (void)ctx;
    if (!ai_data) {
        return;
    }

    type_str = ai_data->type;
    type_len = ai_data->type_len > 0 ? (size_t)ai_data->type_len : 0;
    data_str = ai_data->data;
    data_len = ai_data->data_len > 0 ? (size_t)ai_data->data_len : 0;
    is_mcp = nertc_ai_type_matches(type_str, type_len, "mcp");

    NERTC_LOGI("AI data received: type=%s, data=%s", type_str, data_str);

    if (is_mcp && data_str) {
        nertc_handle_mcp_data(data_str, data_len);
    } else if (nertc_ai_type_matches(type_str, type_len, "updateSongList") && data_str) {
        nertc_handle_song_list_data(data_str, data_len);
    } else if (nertc_ai_type_matches(type_str, type_len, "songSearch") && data_str) {
        nertc_handle_song_search_data(data_str, data_len);
    }

    if (!g_ctx.audio_channel_opened &&
        !is_mcp &&
        !nertc_ai_type_matches(type_str, type_len, "updateSongList") &&
        !nertc_ai_type_matches(type_str, type_len, "songSearch")) {
        NERTC_LOGW("AI data received but audio channel not opened");
        return;
    }

    if (nertc_ai_type_matches(type_str, type_len, "tts") && data_str) {
        if (nertc_ai_data_contains(data_str, data_len, "state") &&
            nertc_ai_data_contains(data_str, data_len, "\"start\"")) {
            dispatch_event(NERTC_PROTOCOL_EVENT_TTS_START);
        } else if (nertc_ai_data_contains(data_str, data_len, "state") &&
                   nertc_ai_data_contains(data_str, data_len, "stop")) {
            dispatch_event(NERTC_PROTOCOL_EVENT_TTS_STOP);
        }
    } else if (nertc_ai_type_matches(type_str, type_len, "tool") && data_str) {
        if ((nertc_ai_data_contains(data_str, data_len, "toolCalls") &&
             nertc_ai_data_contains(data_str, data_len, "Long_Silence")) ||
            (nertc_ai_data_contains(data_str, data_len, "toolCalls") &&
             nertc_ai_data_contains(data_str, data_len, "good_bye_call"))) {
            nertc_protocol_close_audio_channel();
        }
    }

    if (g_ctx.event_callback) {
        nertc_protocol_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.event = NERTC_PROTOCOL_EVENT_AI_DATA;
        evt.data.ai.type = ai_data->type;
        evt.data.ai.type_len = ai_data->type_len;
        evt.data.ai.data = ai_data->data;
        evt.data.ai.data_len = ai_data->data_len;
        g_ctx.event_callback(&evt, g_ctx.event_user_data);
    }
}

static void nertc_on_audio_encoded_data(const nertc_sdk_callback_context_t *ctx,
                                        uint64_t uid,
                                        nertc_sdk_media_stream_e stream_type,
                                        nertc_sdk_audio_encoded_frame_t *encoded_frame,
                                        bool is_mute_packet)
{
    (void)ctx;
    (void)stream_type;

    if (!encoded_frame || !encoded_frame->data || encoded_frame->length <= 0) {
        return;
    }

    if (!g_ctx.audio_channel_opened) {
        return;
    }

    dispatch_audio_data_event(uid, encoded_frame, is_mute_packet);
}

void nertc_protocol_config_init(nertc_protocol_config_t *config)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->app_key = APP_KEY;
    config->device_id = APP_DEFAULT_DEVICE_ID;
    config->sample_rate = NERTC_DEFAULT_SAMPLE_RATE;
    config->out_sample_rate = NERTC_DEFAULT_OUT_SAMPLE_RATE;
    config->channels = NERTC_DEFAULT_CHANNELS;
    config->frame_duration = NERTC_DEFAULT_FRAME_DURATION;
    config->force_unsafe_mode = true;
    config->enable_server_aec = false;
    config->prefer_use_psram = true;
    config->enable_asr = true;
    config->enable_mcp_server = true;
    // config->custom_config = NERTC_DEFAULT_CUSTOM_CONFIG;
}

int nertc_protocol_init(void)
{
    nertc_protocol_config_t config;

    nertc_protocol_config_init(&config);
    return nertc_protocol_init_with_config(&config);
}

int nertc_protocol_init_with_config(const nertc_protocol_config_t *config)
{
    nertc_sdk_configuration_t sdk_cfg;
    nertc_sdk_engine_config_t engine_cfg;
    int ret;

    if (g_ctx.initialized) {
        NERTC_LOGW("Already initialized");
        return 0;
    }

    if (!config || !config->app_key || !config->device_id) {
        NERTC_LOGE("Invalid config: app_key and device_id required");
        return -1;
    }

    NERTC_LOGI("Protocol init start...");
    NERTC_LOGI("SDK Version: %s", nertc_get_version());

    memset(&g_ctx, 0, sizeof(g_ctx));
    if (os_sem_create(&g_ctx.join_sem, 0) != OS_NO_ERR) {
        NERTC_LOGE("Failed to create join semaphore");
        return -1;
    }

    g_ctx.config = *config;
    g_ctx.asr_enabled = config->enable_asr;
    g_ctx.server_sample_rate = config->sample_rate > 0 ? config->sample_rate : NERTC_DEFAULT_SAMPLE_RATE;
    g_ctx.server_out_sample_rate = config->out_sample_rate > 0 ? config->out_sample_rate : NERTC_DEFAULT_OUT_SAMPLE_RATE;
    g_ctx.server_frame_duration = config->frame_duration > 0 ? config->frame_duration : NERTC_DEFAULT_FRAME_DURATION;

    nertc_sdk_configuration_init(&sdk_cfg);
    sdk_cfg.app_key = config->app_key;
    sdk_cfg.device_id = config->device_id;
    sdk_cfg.force_unsafe_mode = config->force_unsafe_mode;
    sdk_cfg.licence_cfg.license = config->license;
    sdk_cfg.audio_config.sample_rate = g_ctx.server_sample_rate;
    sdk_cfg.audio_config.out_sample_rate = g_ctx.server_out_sample_rate;
    sdk_cfg.audio_config.channels = config->channels > 0 ? config->channels : NERTC_DEFAULT_CHANNELS;
    sdk_cfg.audio_config.frame_duration = g_ctx.server_frame_duration;
    sdk_cfg.audio_config.codec_type = NERTC_SDK_AUDIO_CODEC_TYPE_OPUS;
    sdk_cfg.optional_config.device_performance_level = NERTC_SDK_DEVICE_LEVEL_HIGH;
    sdk_cfg.optional_config.prefer_use_psram = config->prefer_use_psram;
    sdk_cfg.optional_config.enable_server_aec = config->enable_server_aec;
    sdk_cfg.optional_config.custom_config = config->custom_config;
    sdk_cfg.mqtt_config.endpoint = config->mqtt_endpoint;
    sdk_cfg.mqtt_config.client_id = config->mqtt_client_id;
    sdk_cfg.mqtt_config.username = config->mqtt_username;
    sdk_cfg.mqtt_config.password = config->mqtt_password;
    sdk_cfg.mqtt_config.publish_topic = config->mqtt_publish_topic;
    sdk_cfg.log_cfg.log_level = NERTC_SDK_LOG_INFO;

    nertc_mqtt_ext_init();
    g_ctx.engine = nertc_create_engine_with_config(&sdk_cfg);
    if (!g_ctx.engine) {
        NERTC_LOGE("Failed to create NERtc engine");
        nertc_mqtt_ext_deinit();
        return -2;
    }

    nertc_sdk_engine_config_init(&engine_cfg);
    engine_cfg.engine_mode = NERTC_SDK_ENGINE_MODE_LITE;
    engine_cfg.feature_config.enable_mcp_server = config->enable_mcp_server;
    engine_cfg.event_handler.on_error = nertc_on_error;
    engine_cfg.event_handler.on_license_expire_warning = nertc_on_license_expire_warning;
    engine_cfg.event_handler.on_channel_status_changed = nertc_on_channel_status_changed;
    engine_cfg.event_handler.on_join = nertc_on_join;
    engine_cfg.event_handler.on_disconnect = nertc_on_disconnect;
    engine_cfg.event_handler.on_user_joined = nertc_on_user_joined;
    engine_cfg.event_handler.on_user_left = nertc_on_user_left;
    engine_cfg.event_handler.on_user_audio_start = nertc_on_user_audio_start;
    engine_cfg.event_handler.on_user_audio_mute = nertc_on_user_audio_mute;
    engine_cfg.event_handler.on_user_audio_stop = nertc_on_user_audio_stop;
    engine_cfg.event_handler.on_asr_caption_state_changed = nertc_on_asr_caption_state_changed;
    engine_cfg.event_handler.on_asr_caption_result = nertc_on_asr_caption_result;
    engine_cfg.event_handler.on_ai_data = nertc_on_ai_data;
    engine_cfg.event_handler.on_audio_encoded_data = nertc_on_audio_encoded_data;
    engine_cfg.event_handler.on_server_time = NULL;
    engine_cfg.user_data = &g_ctx;
    engine_cfg.ext_net_handle = nertc_mqtt_ext_get_handle();

    ret = nertc_init_engine(g_ctx.engine, &engine_cfg);
    if (ret != 0) {
        NERTC_LOGE("Failed to initialize NERtc SDK, error: %d", ret);
        nertc_destroy_engine(g_ctx.engine);
        g_ctx.engine = NULL;
        nertc_mqtt_ext_deinit();
        return -3;
    }

    g_ctx.initialized = true;
    mcp_server_init();
    mcp_server_set_send_func(nertc_protocol_reply_mcp_tool);
    music_player_init();
    NERTC_LOGI("Protocol init success, version: %s", nertc_get_version());
    return 0;
}

void nertc_protocol_deinit(void)
{
    NERTC_LOGI("Protocol deinit");

    if (g_ctx.joined) {
        nertc_protocol_stop();
    }

    music_player_deinit();

    if (g_ctx.engine) {
        nertc_destroy_engine(g_ctx.engine);
        g_ctx.engine = NULL;
    }

    nertc_mqtt_ext_deinit();
    memset(&g_ctx, 0, sizeof(g_ctx));
}

void nertc_protocol_set_event_callback(nertc_protocol_event_callback_t callback, void *user_data)
{
    g_ctx.event_callback = callback;
    g_ctx.event_user_data = user_data;
}

void nertc_protocol_set_audio_callback(nertc_protocol_audio_callback_t callback, void *user_data)
{
    g_ctx.audio_callback = callback;
    g_ctx.audio_user_data = user_data;
}

int nertc_protocol_start(const char *cname, uint64_t uid)
{
    uint64_t join_uid;
    int ret;

    if (!g_ctx.initialized || !g_ctx.engine) {
        NERTC_LOGE("Not initialized");
        return -1;
    }

    if (g_ctx.joined) {
        NERTC_LOGW("Already joined");
        return 0;
    }

    if (cname) {
        strncpy(g_ctx.cname, cname, sizeof(g_ctx.cname) - 1);
    } else {
        extern u32 timer_get_ms(void);
        uint32_t hw_rand = JL_RAND->R64L ^ JL_RAND->R64H ^ timer_get_ms();
        uint32_t random_num = 100000 + (hw_rand % 900000);
        snprintf(g_ctx.cname, sizeof(g_ctx.cname), "80%u", random_num);
    }

    join_uid = (uid > 0) ? uid : NERTC_DEFAULT_UID;
    g_ctx.join_result = -1;

    ret = nertc_join(g_ctx.engine, g_ctx.cname, "", join_uid);
    if (ret != 0) {
        NERTC_LOGE("Join failed, error: %d", ret);
        return ret;
    }

    NERTC_LOGI("Join requested, cname=%s, uid=%llu",
               g_ctx.cname,
               (unsigned long long)join_uid);
    ret = os_sem_pend(&g_ctx.join_sem, NERTC_JOIN_TIMEOUT_MS);
    if (ret != OS_NO_ERR) {
        NERTC_LOGE("Join timeout or error: %d", ret);
        return -2;
    }

    if (g_ctx.join_result != 0) {
        NERTC_LOGE("Join failed with code: %d", g_ctx.join_result);
        return g_ctx.join_result;
    }

    return 0;
}

int nertc_protocol_stop(void)
{
    int ret;

    if (!g_ctx.engine) {
        return -1;
    }

    if (g_ctx.audio_channel_opened) {
        nertc_protocol_close_audio_channel();
    }

    ret = nertc_leave(g_ctx.engine);
    g_ctx.joined = false;
    g_ctx.audio_channel_opened = false;
    return ret;
}

int nertc_protocol_open_audio_channel(void)
{
    nertc_sdk_asr_caption_config_t asr_cfg;
    int ret;

    if (!g_ctx.engine || !g_ctx.joined) {
        NERTC_LOGE("Cannot open audio channel: engine=%p, joined=%d", g_ctx.engine, g_ctx.joined);
        return -1;
    }

    if (g_ctx.audio_channel_opened) {
        NERTC_LOGW("Audio channel already opened");
        return 0;
    }

    audio_io_init(g_ctx.server_sample_rate, g_ctx.config.channels);
    ret = nertc_start_ai_with_config(g_ctx.engine, NULL);
    if (ret != 0) {
        NERTC_LOGE("Start AI failed, error: %d", ret);
        return ret;
    }

    if (g_ctx.asr_enabled) {
        nertc_sdk_asr_caption_config_init(&asr_cfg);
        ret = nertc_start_asr_caption(g_ctx.engine, &asr_cfg);
        if (ret != 0) {
            NERTC_LOGE("Start ASR caption failed, error: %d", ret);
        }
    }

    g_ctx.audio_channel_opened = true;
    dispatch_event(NERTC_PROTOCOL_EVENT_AI_READY);
    return 0;
}

void nertc_protocol_close_audio_channel(void)
{
    if (!g_ctx.engine) {
        return;
    }

    if (!g_ctx.audio_channel_opened) {
        return;
    }

    NERTC_LOGI("CloseAudioChannel");
    nertc_stop_ai(g_ctx.engine);
    if (g_ctx.asr_enabled) {
        nertc_stop_asr_caption(g_ctx.engine);
    }
    audio_io_stop();
    g_ctx.audio_channel_opened = false;
}

void nertc_protocol_restore_audio_channel(void)
{
    if (!g_ctx.engine || !g_ctx.joined || g_ctx.audio_channel_opened) {
        return;
    }

    NERTC_LOGI("RestoreAudioChannel");
    audio_io_start();
    nertc_protocol_open_audio_channel();
}

bool nertc_protocol_is_audio_channel_opened(void)
{
    return g_ctx.joined && g_ctx.audio_channel_opened;
}

int nertc_protocol_push_audio_pcm(const int16_t *data, int length)
{
    nertc_sdk_audio_frame_t frame;

    if (!g_ctx.engine || !g_ctx.joined) {
        return -1;
    }

    nertc_sdk_audio_frame_init(&frame);
    frame.type = NERTC_SDK_AUDIO_PCM_16;
    frame.config.sample_rate = g_ctx.server_sample_rate > 0 ? g_ctx.server_sample_rate : NERTC_DEFAULT_SAMPLE_RATE;
    frame.config.out_sample_rate = g_ctx.server_out_sample_rate > 0 ? g_ctx.server_out_sample_rate : NERTC_DEFAULT_OUT_SAMPLE_RATE;
    frame.config.channels = g_ctx.config.channels > 0 ? g_ctx.config.channels : NERTC_DEFAULT_CHANNELS;
    frame.config.frame_duration = g_ctx.server_frame_duration > 0 ? g_ctx.server_frame_duration : NERTC_DEFAULT_FRAME_DURATION;
    frame.config.samples_per_channel = g_ctx.samples_per_channel > 0
        ? g_ctx.samples_per_channel
        : (frame.config.sample_rate * frame.config.frame_duration / 1000);
    frame.config.codec_type = NERTC_SDK_AUDIO_CODEC_TYPE_OPUS;
    frame.data = (void *)data;
    frame.length = length;

    return nertc_push_audio_frame(g_ctx.engine, NERTC_SDK_MEDIA_MAIN_AUDIO, &frame);
}

int nertc_protocol_push_audio_encoded(const uint8_t *data, int length, uint32_t timestamp)
{
    nertc_sdk_audio_encoded_frame_t frame;
    nertc_sdk_audio_config_t audio_cfg;
    int ret;

    if (!g_ctx.engine || !g_ctx.joined) {
        return -1;
    }

    if (!g_ctx.audio_channel_opened) {
        return 0;
    }

    nertc_sdk_audio_encoded_frame_init(&frame);
    frame.data = (nertc_sdk_audio_data_t *)data;
    frame.length = length;
    frame.encoded_timestamp = timestamp;

    memset(&audio_cfg, 0, sizeof(audio_cfg));
    audio_cfg.sample_rate = g_ctx.server_sample_rate > 0 ? g_ctx.server_sample_rate : NERTC_DEFAULT_SAMPLE_RATE;
    audio_cfg.frame_duration = g_ctx.server_frame_duration > 0 ? g_ctx.server_frame_duration : NERTC_DEFAULT_FRAME_DURATION;
    audio_cfg.channels = g_ctx.config.channels > 0 ? g_ctx.config.channels : NERTC_DEFAULT_CHANNELS;
    audio_cfg.samples_per_channel = g_ctx.samples_per_channel > 0
        ? g_ctx.samples_per_channel
        : (audio_cfg.sample_rate * audio_cfg.frame_duration / 1000);
    audio_cfg.out_sample_rate = g_ctx.server_out_sample_rate > 0
        ? g_ctx.server_out_sample_rate
        : NERTC_DEFAULT_OUT_SAMPLE_RATE;
    audio_cfg.codec_type = NERTC_SDK_AUDIO_CODEC_TYPE_OPUS;

    ret = nertc_push_audio_encoded_frame(g_ctx.engine, NERTC_SDK_MEDIA_MAIN_AUDIO, audio_cfg, 100, &frame);
    if (ret == 0) {
        return length;
    }

    NERTC_LOGI("Push audio encoded failed, error: %d", ret);
    return 0;
}

int nertc_protocol_push_aec_reference(const uint8_t *encoded_data, int encoded_len,
                                      const int16_t *pcm_data, int pcm_len,
                                      int64_t timestamp)
{
    nertc_sdk_audio_encoded_frame_t encoded_frame;
    nertc_sdk_audio_frame_t pcm_frame;

    if (!g_ctx.engine || !g_ctx.joined) {
        return -1;
    }

    nertc_sdk_audio_encoded_frame_init(&encoded_frame);
    encoded_frame.data = (nertc_sdk_audio_data_t *)encoded_data;
    encoded_frame.length = encoded_len;
    encoded_frame.timestamp_ms = timestamp;

    nertc_sdk_audio_frame_init(&pcm_frame);
    pcm_frame.type = NERTC_SDK_AUDIO_PCM_16;
    pcm_frame.config.sample_rate = g_ctx.server_sample_rate > 0 ? g_ctx.server_sample_rate : NERTC_DEFAULT_SAMPLE_RATE;
    pcm_frame.config.out_sample_rate = g_ctx.server_out_sample_rate > 0 ? g_ctx.server_out_sample_rate : NERTC_DEFAULT_OUT_SAMPLE_RATE;
    pcm_frame.config.channels = g_ctx.config.channels > 0 ? g_ctx.config.channels : NERTC_DEFAULT_CHANNELS;
    pcm_frame.config.frame_duration = g_ctx.server_frame_duration > 0 ? g_ctx.server_frame_duration : NERTC_DEFAULT_FRAME_DURATION;
    pcm_frame.config.samples_per_channel = g_ctx.samples_per_channel > 0
        ? g_ctx.samples_per_channel
        : (pcm_frame.config.sample_rate * pcm_frame.config.frame_duration / 1000);
    pcm_frame.data = (void *)pcm_data;
    pcm_frame.length = pcm_len;

    return nertc_push_audio_reference_frame(g_ctx.engine, NERTC_SDK_MEDIA_MAIN_AUDIO,
                                            &encoded_frame, &pcm_frame);
}

int nertc_protocol_send_tts(const char *text, int interrupt_mode, bool add_context)
{
    if (!g_ctx.engine || !g_ctx.joined) {
        return -1;
    }

    if (!text) {
        return -2;
    }

    return nertc_ai_external_tts(g_ctx.engine, text, interrupt_mode, add_context);
}

int nertc_protocol_send_llm_text(const char *text, int interrupt_mode)
{
    if (!g_ctx.engine || !g_ctx.joined) {
        return -1;
    }

    if (!text) {
        return -2;
    }

    return nertc_ai_llm_prompt(g_ctx.engine, text, interrupt_mode);
}

int nertc_protocol_manual_interrupt(void)
{
    if (!g_ctx.engine) {
        return -1;
    }

    return nertc_ai_manual_interrupt(g_ctx.engine);
}

int nertc_protocol_manual_start_listen(void)
{
    if (!g_ctx.engine) {
        return -1;
    }

    return nertc_ai_manual_start_listen(g_ctx.engine);
}

int nertc_protocol_manual_stop_listen(void)
{
    if (!g_ctx.engine) {
        return -1;
    }

    return nertc_ai_manual_stop_listen(g_ctx.engine);
}

int nertc_protocol_reply_mcp_tool(const char *payload, int payload_len)
{
    nertc_sdk_mcp_tool_result_t result;

    if (!g_ctx.engine) {
        return -1;
    }

    if (!payload || payload_len <= 0) {
        return -2;
    }

    nertc_sdk_mcp_tool_result_init(&result);
    result.payload = payload;
    result.payload_len = payload_len;

    return nertc_ai_reply_mcp_tool_call(g_ctx.engine, &result);
}

const char *nertc_protocol_get_version(void)
{
    return nertc_get_version();
}

const char *nertc_protocol_get_cname(void)
{
    if (g_ctx.joined && g_ctx.cname[0] != '\0') {
        return g_ctx.cname;
    }
    return NULL;
}

bool nertc_protocol_is_joined(void)
{
    return g_ctx.joined;
}
