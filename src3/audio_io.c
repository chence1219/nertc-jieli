#include "audio_io.h"

#include "app_config.h"
#include "audio_input.h"
#include "nertc_log.h"
#include "nertc_protocol.h"

#define TAG "AUDIO"
#define AUDIO_IO_READ_SIZE 120

struct recorder_hdl {
    volatile u8 is_enc_run;
};

static struct recorder_hdl g_recorder;
static int g_uplink_pid;
static volatile u8 g_manual_stop;
static int g_sample_rate = NERTC_DEFAULT_SAMPLE_RATE;
static int g_channels = NERTC_DEFAULT_CHANNELS;

static int audio_io_send_uplink(const void *data, unsigned int len)
{
    return nertc_protocol_push_audio_encoded((const uint8_t *)data, (int)len, 0);
}

static void audio_io_uplink_thread(void *arg)
{
    static u8 audio_buffer[AUDIO_IO_READ_SIZE];
    int read_len;
    int send_len;
    int retry_times = 0;

    (void)arg;

    audio_stream_init(g_sample_rate, 16, g_channels);
    start_audio_stream();

    while (g_recorder.is_enc_run) {
        read_len = _device_get_voice_data(audio_buffer, AUDIO_IO_READ_SIZE);
        if (!g_recorder.is_enc_run || read_len <= 0) {
            continue;
        }

        if (!nertc_protocol_is_audio_channel_opened()) {
            continue;
        }

        send_len = audio_io_send_uplink(audio_buffer, (unsigned int)read_len);
        if (send_len <= 0) {
            if (++retry_times >= 10) {
                NERTC_LOGE("audio uplink stalled, forcing stop");
                audio_io_stop();
                break;
            }
            continue;
        }

        retry_times = 0;
    }

    mdelay(100);
    if (!g_manual_stop) {
        NERTC_LOGE("audio drive thread exited unexpectedly");
    }
    g_manual_stop = 0;
    g_uplink_pid = 0;
}

void audio_io_init(int sample_rate, int channels)
{
    if (sample_rate > 0) {
        g_sample_rate = sample_rate;
    }
    if (channels > 0) {
        g_channels = channels;
    }
}

void audio_io_start(void)
{
    if (g_recorder.is_enc_run) {
        return;
    }

    g_manual_stop = 0;
    g_recorder.is_enc_run = 1;

    if (thread_fork("Volc_demo", 4, 3 * 1024, 0, &g_uplink_pid,
                    audio_io_uplink_thread, NULL) != OS_NO_ERR) {
        g_recorder.is_enc_run = 0;
        g_uplink_pid = 0;
        NERTC_LOGE("failed to create audio drive thread");
    }
}

void audio_io_stop(void)
{
    if (!g_recorder.is_enc_run) {
        return;
    }

    g_manual_stop = 1;
    g_recorder.is_enc_run = 0;
    mdelay(100);
    stop_audio_stream();
}

u8 audio_io_is_started(void)
{
    return g_recorder.is_enc_run ? 1 : 0;
}

int audio_io_push_downlink(const uint8_t *data, int len)
{
    if (!data || len <= 0) {
        return -1;
    }

    return _device_write_voice_data((void *)data, (unsigned int)len);
}
