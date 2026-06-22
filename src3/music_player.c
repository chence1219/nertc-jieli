#include "music_player.h"

#include "app_config.h"
#include "audio_input.h"
#include "cJSON.h"
#include "lwip.h"
#include "nertc_log.h"
#include "nertc_protocol.h"
#include "network_download/net_download.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "system/includes.h"
#include "system/wait.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MUSIC"

#define MUSIC_PLAYER_MAX_SONGS 50
#define MUSIC_PLAYER_URL_LEN 512
#define MUSIC_PLAYER_NAME_LEN 128
#define MUSIC_PLAYER_ARTIST_LEN 64
#define MUSIC_PLAYER_ALBUM_LEN 64
#define MUSIC_PLAYER_MIN_VOLUME 0
#define MUSIC_PLAYER_MAX_VOLUME 100
#define MUSIC_PLAYER_DEFAULT_VOLUME 70

#if defined(CONFIG_NO_SDRAM_ENABLE)
#define MUSIC_PLAYER_DEC_BUF_LEN (6 * 1024)
#define MUSIC_PLAYER_NET_CBUF_LEN (40 * 1024)
#define MUSIC_PLAYER_NET_TMP_THRESHOLD (10 * 1024)
#else
#define MUSIC_PLAYER_DEC_BUF_LEN (12 * 1024)
#define MUSIC_PLAYER_NET_CBUF_LEN (200 * 1024)
#define MUSIC_PLAYER_NET_TMP_THRESHOLD (80 * 1024)
#endif

#define MUSIC_PLAYER_RELEASE_RETRY_MS 20
#define MUSIC_PLAYER_RELEASE_RETRY_MAX 200

typedef struct {
    char name[MUSIC_PLAYER_NAME_LEN];
    char url[MUSIC_PLAYER_URL_LEN];
    char artist[MUSIC_PLAYER_ARTIST_LEN];
    char album[MUSIC_PLAYER_ALBUM_LEN];
    int duration;
} music_song_info_t;

typedef enum {
    MUSIC_STATE_IDLE = 0,
    MUSIC_STATE_PLAYING,
    MUSIC_STATE_STOPPED,
} music_player_state_t;

typedef struct {
    u8 initialized;
    u16 wait_download;
    u16 wait_release;
    int release_retry_count;
    int download_ready;
    int volume;
    int song_count;
    int current_index;
    u8 play_now;
    music_player_state_t state;
    OS_MUTEX mutex;
    bool mutex_ready;
    struct server *dec_server;
    void *net_file;
    const char *url;
    void *dec_end_file;
    music_song_info_t songs[MUSIC_PLAYER_MAX_SONGS];
} music_player_context_t;

static music_player_context_t g_music = {
    .volume = MUSIC_PLAYER_DEFAULT_VOLUME,
    .current_index = -1,
};

#define __this (&g_music)

extern int lwip_canceladdrinfo(void);

static const struct audio_vfs_ops net_audio_dec_vfs_ops = {
    .fread = net_download_read,
    .fseek = net_download_seek,
    .flen = net_download_get_file_len,
};

static void music_player_lock(void)
{
    if (__this->mutex_ready) {
        os_mutex_pend(&__this->mutex, 0);
    }
}

static void music_player_unlock(void)
{
    if (__this->mutex_ready) {
        os_mutex_post(&__this->mutex);
    }
}

static int music_player_clamp_volume(int volume)
{
    if (volume < MUSIC_PLAYER_MIN_VOLUME) {
        return MUSIC_PLAYER_MIN_VOLUME;
    }
    if (volume > MUSIC_PLAYER_MAX_VOLUME) {
        return MUSIC_PLAYER_MAX_VOLUME;
    }
    return volume;
}

static void music_player_copy_field(char *dst, size_t dst_size, const cJSON *item)
{
    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

static void music_player_clear_list_locked(void)
{
    memset(__this->songs, 0, sizeof(__this->songs));
    __this->song_count = 0;
    __this->current_index = -1;
    __this->play_now = false;
    __this->state = MUSIC_STATE_IDLE;
}

static int music_player_prepare_mutex(void)
{
    if (__this->mutex_ready) {
        return 0;
    }

    if (os_mutex_create(&__this->mutex) != OS_NO_ERR) {
        NERTC_LOGE("music player mutex create failed");
        return -1;
    }

    __this->mutex_ready = true;
    return 0;
}

static void music_player_clear_runtime_state(void)
{
    __this->wait_download = 0;
    __this->wait_release = 0;
    __this->release_retry_count = 0;
    __this->download_ready = 0;
    __this->net_file = NULL;
    __this->url = NULL;
    __this->dec_end_file = NULL;
}

static void music_player_stop_playback_internal(int restore_audio);
static int music_player_open_url(const char *url);
static int music_player_ensure_init(void);

static void music_player_finish_current_track(void)
{
    const char *next_url = NULL;
    int next_index;

    music_player_stop_playback_internal(0);

    music_player_lock();
    if (__this->state == MUSIC_STATE_PLAYING &&
        __this->song_count > 0 &&
        __this->current_index >= 0) {
        next_index = __this->current_index + 1;
        if (next_index < __this->song_count &&
            __this->songs[next_index].url[0] != '\0') {
            __this->current_index = next_index;
            next_url = __this->songs[next_index].url;
        } else {
            __this->state = MUSIC_STATE_IDLE;
        }
    } else {
        __this->state = MUSIC_STATE_IDLE;
    }
    music_player_unlock();

    if (next_url) {
        if (music_player_open_url(next_url) == 0) {
            return;
        }
    }

    nertc_protocol_restore_audio_channel();
}

static void music_player_dec_event_handler(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;

    if (!argv) {
        return;
    }

    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_END:
    case AUDIO_SERVER_EVENT_ERR:
        if (__this->dec_end_file && (void *)argv[1] == __this->dec_end_file) {
            __this->dec_end_file = NULL;
            music_player_finish_current_track();
        }
        break;
    default:
        break;
    }
}

static int music_player_fail_play(const char *reason, int err)
{
    NERTC_LOGE("music play failed: %s err=%d", reason ? reason : "unknown", err);
    music_player_stop_playback_internal(0);

    music_player_lock();
    __this->state = MUSIC_STATE_IDLE;
    music_player_unlock();

    nertc_protocol_restore_audio_channel();
    return -EFAULT;
}

static int music_player_download_ready(void *priv)
{
    (void)priv;

    __this->download_ready = net_download_check_ready(__this->net_file);
    return __this->download_ready ? 1 : 0;
}

static int music_player_open_decoder(void);

static void music_player_retry_open_timer(void *priv)
{
    int status;

    (void)priv;

    if (!__this->wait_release || !__this->net_file) {
        return;
    }

    status = get_audio_play_status();
    if (status != AUDIO_DEC_STOP) {
        if (++__this->release_retry_count >= MUSIC_PLAYER_RELEASE_RETRY_MAX) {
            sys_timer_del(__this->wait_release);
            __this->wait_release = 0;
            music_player_fail_play("timeout waiting audio decoder stop", status);
        }
        return;
    }

    sys_timer_del(__this->wait_release);
    __this->wait_release = 0;
    __this->release_retry_count = 0;
    music_player_open_decoder();
}

static int music_player_open_decoder_done(void *priv)
{
    (void)priv;
    return music_player_open_decoder();
}

static int music_player_open_decoder(void)
{
    union audio_req req = {0};
    int status;
    int err;

    __this->wait_download = 0;

    if (__this->download_ready < 0 || !__this->net_file || !__this->dec_server) {
        return music_player_fail_play("download not ready", __this->download_ready);
    }

    status = get_audio_play_status();
    if (status != AUDIO_DEC_STOP) {
        if (!__this->wait_release) {
            __this->release_retry_count = 0;
            __this->wait_release = sys_timer_add_to_task("app_core", NULL,
                                                         music_player_retry_open_timer,
                                                         MUSIC_PLAYER_RELEASE_RETRY_MS);
            if (!__this->wait_release) {
                return music_player_fail_play("schedule decoder release wait failed", status);
            }
        }
        return 0;
    }

    req.dec.dec_type = net_download_get_media_type(__this->net_file);
    if (!req.dec.dec_type) {
        return music_player_fail_play("media type detect failed", 0);
    }

    net_download_set_read_timeout(__this->net_file, 5000);

    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = __this->volume;
    req.dec.digital_volume = 100;
    req.dec.output_buf = NULL;
    req.dec.output_buf_len = MUSIC_PLAYER_DEC_BUF_LEN;
    req.dec.file = (FILE *)__this->net_file;
    req.dec.channel = 0;
    req.dec.sample_rate = 0;
    req.dec.priority = 1;
    req.dec.vfs_ops = &net_audio_dec_vfs_ops;
#if defined(CONFIG_AUDIO_DEC_PLAY_SOURCE)
    req.dec.sample_source = CONFIG_AUDIO_DEC_PLAY_SOURCE;
#else
    req.dec.sample_source = "dac";
#endif

#if TCFG_EQ_ENABLE
#if defined(EQ_CORE_V1)
    req.dec.attr |= AUDIO_ATTR_EQ_EN;
#if TCFG_LIMITER_ENABLE
    req.dec.attr |= AUDIO_ATTR_EQ32BIT_EN;
#endif
#if TCFG_DRC_ENABLE
    req.dec.attr |= AUDIO_ATTR_DRC_EN;
#endif
#endif
#endif

#ifdef CONFIG_DEC_DIGITAL_VOLUME_ENABLE
    req.dec.effect |= AUDIO_EFFECT_DIGITAL_VOL;
#endif

#ifdef CONFIG_SPECTRUM_FFT_EFFECT_ENABLE
    req.dec.effect |= AUDIO_EFFECT_SPECTRUM_FFT;
#endif

    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        return music_player_fail_play("audio decoder open failed", err);
    }

    net_download_set_read_timeout(__this->net_file, 0);

    memset(&req, 0, sizeof(req));
#ifdef CONFIG_DEC_ANALOG_VOLUME_ENABLE
    req.dec.attr |= AUDIO_ATTR_FADE_INOUT;
#endif
    req.dec.cmd = AUDIO_DEC_START;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

    net_download_set_pp(__this->net_file, 0);
    net_download_set_tmp_data_threshold(__this->net_file, MUSIC_PLAYER_NET_TMP_THRESHOLD);
    return 0;
}

static void music_player_stop_playback_internal(int restore_audio)
{
    union audio_req req = {0};
    int argv[2];

    if (__this->wait_release) {
        sys_timer_del(__this->wait_release);
        __this->wait_release = 0;
    }

    if (!__this->net_file) {
        __this->release_retry_count = 0;
        __this->download_ready = 0;
        __this->url = NULL;
        __this->dec_end_file = NULL;
        if (restore_audio) {
            nertc_protocol_restore_audio_channel();
        }
        return;
    }

    net_download_buf_inactive(__this->net_file);

    if (__this->wait_download) {
        wait_completion_del(__this->wait_download);
        __this->wait_download = 0;
    } else if (__this->dec_server) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

        argv[0] = AUDIO_SERVER_EVENT_END;
        argv[1] = (int)__this->net_file;
        server_event_handler_del(__this->dec_server, 2, argv);
    }

    lwip_canceladdrinfo();
    net_download_close(__this->net_file);

    music_player_clear_runtime_state();
    if (restore_audio) {
        nertc_protocol_restore_audio_channel();
    }
}

static int music_player_open_url(const char *url)
{
    struct net_download_parm parm;
    int err;

    if (!url || url[0] == '\0') {
        return -1;
    }

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    music_player_stop_playback_internal(0);
    nertc_protocol_close_audio_channel();

    memset(&parm, 0, sizeof(parm));
    parm.url = url;
    parm.prio = 0;
    parm.cbuf_size = MUSIC_PLAYER_NET_CBUF_LEN;
    parm.timeout_millsec = 10000;
    parm.seek_threshold = 1024 * 200;

    err = net_download_open(&__this->net_file, &parm);
    if (err) {
        NERTC_LOGE("net_download_open failed: %d", err);
        music_player_lock();
        __this->state = MUSIC_STATE_IDLE;
        music_player_unlock();
        nertc_protocol_restore_audio_channel();
        return err;
    }

    __this->url = url;
    __this->dec_end_file = __this->net_file;
    __this->download_ready = 0;
    __this->wait_download = wait_completion_add_to_task("app_core",
                                                        music_player_download_ready,
                                                        music_player_open_decoder_done,
                                                        NULL, NULL);
    if (!__this->wait_download) {
        return music_player_fail_play("create download wait failed", 0);
    }

    return 0;
}

static int music_player_parse_song_list(cJSON *json)
{
    cJSON *song_list_json;
    cJSON *need_confirm_json;
    int count;
    int i;

    if (!json) {
        return -1;
    }

    song_list_json = cJSON_GetObjectItem(json, "songList");
    if (!cJSON_IsArray(song_list_json)) {
        song_list_json = cJSON_GetObjectItem(json, "song_list");
    }
    if (!cJSON_IsArray(song_list_json)) {
        return -1;
    }

    need_confirm_json = cJSON_GetObjectItem(json, "need_confirm");
    count = cJSON_GetArraySize(song_list_json);
    if (count > MUSIC_PLAYER_MAX_SONGS) {
        count = MUSIC_PLAYER_MAX_SONGS;
    }

    music_player_lock();
    music_player_clear_list_locked();
    if (cJSON_IsBool(need_confirm_json)) {
        __this->play_now = !cJSON_IsTrue(need_confirm_json);
    } else if (cJSON_IsNumber(need_confirm_json)) {
        __this->play_now = (need_confirm_json->valueint == 0);
    } else {
        __this->play_now = false;
    }

    for (i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(song_list_json, i);
        cJSON *name_json;
        cJSON *url_json;
        cJSON *artist_json;
        cJSON *album_json;
        cJSON *duration_json;

        if (!item) {
            continue;
        }

        name_json = cJSON_GetObjectItem(item, "name");
        if (!cJSON_IsString(name_json)) {
            name_json = cJSON_GetObjectItem(item, "title");
        }
        url_json = cJSON_GetObjectItem(item, "url");
        artist_json = cJSON_GetObjectItem(item, "artist");
        album_json = cJSON_GetObjectItem(item, "album");
        duration_json = cJSON_GetObjectItem(item, "duration");

        music_player_copy_field(__this->songs[i].name, sizeof(__this->songs[i].name), name_json);
        music_player_copy_field(__this->songs[i].url, sizeof(__this->songs[i].url), url_json);
        music_player_copy_field(__this->songs[i].artist, sizeof(__this->songs[i].artist), artist_json);
        music_player_copy_field(__this->songs[i].album, sizeof(__this->songs[i].album), album_json);
        __this->songs[i].duration = cJSON_IsNumber(duration_json) ? duration_json->valueint : -1;
    }

    __this->song_count = count;
    music_player_unlock();
    return count;
}

static int music_player_select_index(int index)
{
    int ret = -1;

    music_player_lock();
    if (index >= 0 &&
        index < __this->song_count &&
        __this->songs[index].url[0] != '\0') {
        __this->current_index = index;
        __this->state = MUSIC_STATE_PLAYING;
        ret = 0;
    }
    music_player_unlock();

    return ret;
}

static int music_player_ensure_init(void)
{
    if (__this->initialized) {
        return 0;
    }

    if (music_player_prepare_mutex() != 0) {
        return -1;
    }

    music_player_lock();
    music_player_clear_list_locked();
    music_player_unlock();

    __this->volume = MUSIC_PLAYER_DEFAULT_VOLUME;
    __this->dec_server = server_open("audio_server", "dec");
    if (!__this->dec_server) {
        NERTC_LOGE("open decoder server failed");
        return -1;
    }

    server_register_event_handler_to_task(__this->dec_server, NULL,
                                          music_player_dec_event_handler, "app_core");
    __this->initialized = 1;
    return 0;
}

void music_player_init(void)
{
    music_player_ensure_init();
}

void music_player_deinit(void)
{
    if (!__this->initialized) {
        return;
    }

    music_player_stop_playback_internal(0);

    if (__this->dec_server) {
        server_close(__this->dec_server);
        __this->dec_server = NULL;
    }

    music_player_lock();
    music_player_clear_list_locked();
    music_player_unlock();
    music_player_clear_runtime_state();
    __this->volume = MUSIC_PLAYER_DEFAULT_VOLUME;
    __this->initialized = 0;
}

int music_player_update_song_list(const char *json_text)
{
    cJSON *root;
    int count;

    if (!json_text || music_player_ensure_init() != 0) {
        return -1;
    }

    root = cJSON_Parse(json_text);
    if (!root) {
        NERTC_LOGE("music_player_update_song_list parse failed");
        return -1;
    }

    count = music_player_parse_song_list(root);
    cJSON_Delete(root);
    return count;
}

int music_player_maybe_play_first(void)
{
    int should_play = 0;
    int has_song = 0;

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    music_player_lock();
    should_play = __this->play_now ? 1 : 0;
    has_song = (__this->song_count > 0) ? 1 : 0;
    music_player_unlock();

    if (!should_play || !has_song) {
        return 0;
    }

    return music_player_play_index(0);
}

void music_player_clear_song_list(void)
{
    music_player_stop_playback_internal(0);
    music_player_lock();
    music_player_clear_list_locked();
    music_player_unlock();
}

int music_player_is_playing(void)
{
    return __this->wait_download || __this->state == MUSIC_STATE_PLAYING;
}

int music_player_play_index(int index)
{
    const char *url;
    int ret;

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    ret = music_player_select_index(index);
    if (ret != 0) {
        return ret;
    }

    music_player_lock();
    url = __this->songs[__this->current_index].url;
    music_player_unlock();

    return music_player_open_url(url);
}

int music_player_stop(void)
{
    if (!__this->initialized) {
        return 0;
    }

    music_player_stop_playback_internal(1);
    music_player_lock();
    __this->state = MUSIC_STATE_STOPPED;
    music_player_unlock();
    return 0;
}

int music_player_next(void)
{
    int next_index;

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    music_player_lock();
    if (__this->song_count <= 0) {
        music_player_unlock();
        return -1;
    }

    next_index = __this->current_index + 1;
    if (next_index >= __this->song_count) {
        next_index = 0;
    }
    music_player_unlock();

    return music_player_play_index(next_index);
}

int music_player_previous(void)
{
    int prev_index;

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    music_player_lock();
    if (__this->song_count <= 0) {
        music_player_unlock();
        return -1;
    }

    prev_index = __this->current_index - 1;
    if (prev_index < 0) {
        prev_index = __this->song_count - 1;
    }
    music_player_unlock();

    return music_player_play_index(prev_index);
}

int music_player_set_volume(int volume)
{
    union audio_req req = {0};

    if (music_player_ensure_init() != 0) {
        return -1;
    }

    __this->volume = music_player_clamp_volume(volume);
    if (__this->dec_server) {
        req.dec.cmd = AUDIO_DEC_SET_VOLUME;
        req.dec.volume = __this->volume;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    }

    return __this->volume;
}

int music_player_get_volume(void)
{
    return __this->volume;
}
