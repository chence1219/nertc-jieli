#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <stdint.h>
#include "system/includes.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_io_init(int sample_rate, int channels);
void audio_io_start(void);
void audio_io_stop(void);
u8 audio_io_is_started(void);
int audio_io_push_downlink(const uint8_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif
