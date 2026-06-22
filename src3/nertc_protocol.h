#ifndef _NERTC_PROTOCOL_JIELI_H_
#define _NERTC_PROTOCOL_JIELI_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NERTC_DEFAULT_UID              6669
#define NERTC_DEFAULT_SAMPLE_RATE      16000
#define NERTC_DEFAULT_OUT_SAMPLE_RATE  16000
#define NERTC_DEFAULT_CHANNELS         1
#define NERTC_DEFAULT_FRAME_DURATION   60

typedef enum {
    NERTC_PROTOCOL_EVENT_NONE = 0,
    NERTC_PROTOCOL_EVENT_ERROR,
    NERTC_PROTOCOL_EVENT_CONNECTED,
    NERTC_PROTOCOL_EVENT_DISCONNECTED,
    NERTC_PROTOCOL_EVENT_CHANNEL_CHANGED,
    NERTC_PROTOCOL_EVENT_USER_JOINED,
    NERTC_PROTOCOL_EVENT_USER_LEFT,
    NERTC_PROTOCOL_EVENT_USER_AUDIO_START,
    NERTC_PROTOCOL_EVENT_USER_AUDIO_STOP,
    NERTC_PROTOCOL_EVENT_AI_READY,
    NERTC_PROTOCOL_EVENT_AI_DATA,
    NERTC_PROTOCOL_EVENT_ASR_RESULT,
    NERTC_PROTOCOL_EVENT_AUDIO_DATA,
    NERTC_PROTOCOL_EVENT_TTS_START,
    NERTC_PROTOCOL_EVENT_TTS_STOP,
    NERTC_PROTOCOL_EVENT_LICENSE_WARNING,
} nertc_protocol_event_e;

typedef struct {
    int error_code;
    const char *error_msg;
} nertc_protocol_error_data_t;

typedef struct {
    uint64_t cid;
    uint64_t uid;
    int sample_rate;
    int out_sample_rate;
    int frame_duration;
    int samples_per_channel;
} nertc_protocol_connected_data_t;

typedef struct {
    int error_code;
    int reason;
} nertc_protocol_disconnected_data_t;

typedef struct {
    uint64_t uid;
    const char *name;
    int user_type;
} nertc_protocol_user_data_t;

typedef struct {
    uint64_t user_id;
    bool is_local_user;
    uint64_t timestamp;
    const char *content;
    bool is_final;
} nertc_protocol_asr_data_t;

typedef struct {
    const char *type;
    int type_len;
    const char *data;
    int data_len;
} nertc_protocol_ai_data_t;

typedef struct {
    uint64_t uid;
    uint8_t *data;
    int length;
    int64_t timestamp_ms;
    uint32_t encoded_timestamp;
    int sample_rate;
    int frame_duration;
    bool is_mute_packet;
} nertc_protocol_audio_data_t;

typedef struct {
    nertc_protocol_event_e event;
    union {
        nertc_protocol_error_data_t error;
        nertc_protocol_connected_data_t connected;
        nertc_protocol_disconnected_data_t disconnected;
        nertc_protocol_user_data_t user;
        nertc_protocol_asr_data_t asr;
        nertc_protocol_ai_data_t ai;
        nertc_protocol_audio_data_t audio;
        int license_remaining_days;
    } data;
} nertc_protocol_event_t;

typedef struct {
    const char *app_key;
    const char *device_id;
    const char *license;
    const char *custom_config;
    const char *mqtt_endpoint;
    const char *mqtt_client_id;
    const char *mqtt_username;
    const char *mqtt_password;
    const char *mqtt_publish_topic;
    int sample_rate;
    int out_sample_rate;
    int channels;
    int frame_duration;
    bool force_unsafe_mode;
    bool enable_server_aec;
    bool prefer_use_psram;
    bool enable_asr;
    bool enable_mcp_server;
    void *user_data;
} nertc_protocol_config_t;

typedef void (*nertc_protocol_event_callback_t)(const nertc_protocol_event_t *event, void *user_data);
typedef void (*nertc_protocol_audio_callback_t)(const nertc_protocol_audio_data_t *audio, void *user_data);

void nertc_protocol_config_init(nertc_protocol_config_t *config);
int nertc_protocol_init(void);
int nertc_protocol_init_with_config(const nertc_protocol_config_t *config);
void nertc_protocol_deinit(void);
void nertc_protocol_set_event_callback(nertc_protocol_event_callback_t callback, void *user_data);
void nertc_protocol_set_audio_callback(nertc_protocol_audio_callback_t callback, void *user_data);
int nertc_protocol_start(const char *cname, uint64_t uid);
int nertc_protocol_stop(void);
int nertc_protocol_open_audio_channel(void);
void nertc_protocol_close_audio_channel(void);
void nertc_protocol_restore_audio_channel(void);
bool nertc_protocol_is_audio_channel_opened(void);
int nertc_protocol_push_audio_pcm(const int16_t *data, int length);
int nertc_protocol_push_audio_encoded(const uint8_t *data, int length, uint32_t timestamp);
int nertc_protocol_push_aec_reference(const uint8_t *encoded_data, int encoded_len,
                                      const int16_t *pcm_data, int pcm_len,
                                      int64_t timestamp);
int nertc_protocol_send_tts(const char *text, int interrupt_mode, bool add_context);
int nertc_protocol_send_llm_text(const char *text, int interrupt_mode);
int nertc_protocol_manual_interrupt(void);
int nertc_protocol_manual_start_listen(void);
int nertc_protocol_manual_stop_listen(void);
int nertc_protocol_reply_mcp_tool(const char *payload, int payload_len);
const char *nertc_protocol_get_version(void);
const char *nertc_protocol_get_cname(void);
bool nertc_protocol_is_joined(void);

#ifdef __cplusplus
}
#endif

#endif
