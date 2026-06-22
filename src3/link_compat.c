#include "system/includes.h"
#include "media/eq/audio_eq.h"

/*
 * Compatibility layer for symbols that are still needed by the first-gen-shaped
 * audio path but are outside the current Phase 2 business scope.
 */

#define UPDATA_MAGIC 0x5A00

u16 update_result_get(void)
{
    return UPDATA_MAGIC;
}

struct audio_eq *enc_ul_eq_open(u32 sample_rate, u8 ch_num)
{
    (void)sample_rate;
    (void)ch_num;
    return NULL;
}
