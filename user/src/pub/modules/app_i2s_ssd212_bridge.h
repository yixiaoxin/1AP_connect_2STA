#ifndef __APP_I2S_SSD212_BRIDGE_H__
#define __APP_I2S_SSD212_BRIDGE_H__

#include <stdint.h>

/*
 * I2S clock-role selection.
 *
 * 0: AIC8800 is I2S slave; SSD212 must output LRCK/BCK.
 * 1: AIC8800 is I2S master; AIC8800 outputs LRCK/BCK.
 *
 * The current SSD212 microphone-compatible test uses slave mode on AIC8800.
 */
#ifndef APP_SSD212_I2S_AIC_MASTER_ENABLE
#define APP_SSD212_I2S_AIC_MASTER_ENABLE       1U
#endif

/*
 * Optional AP-generated 1 kHz sine test source.
 *
 * ENABLE=1: ignore the UAC playback queue for I2S TX and continuously send
 *           a 1 kHz stereo sine wave to SSD212.
 * ENABLE=0: restore the normal UAC-STA -> AP -> SSD212 audio path.
 *
 * AMPLITUDE is the signed PCM16 peak value. Valid range: 0..32767.
 * Examples: 3000 (quiet), 12000 (default), 24000 (loud).
 */
#ifndef APP_SSD212_I2S_TEST_SINE_ENABLE
#define APP_SSD212_I2S_TEST_SINE_ENABLE       0U
#endif

#ifndef APP_SSD212_I2S_TEST_SINE_AMPLITUDE
#define APP_SSD212_I2S_TEST_SINE_AMPLITUDE    8000U
#endif

/* 0x34 is 0 dB in the AIC8800 playback digital-volume register. */
#ifndef APP_SSD212_I2S_TEST_PLAYBACK_VOLUME
#define APP_SSD212_I2S_TEST_PLAYBACK_VOLUME   0x34U
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t capture_callbacks;
    uint32_t playback_callbacks;
    uint32_t capture_frames;
    uint32_t playback_frames;
    uint32_t ssd212_to_uac_blocks;
    uint32_t ssd212_to_uac_drop;
    uint32_t uac_to_ssd212_blocks;
    uint32_t uac_to_ssd212_underflow;
    uint32_t capture_queue_overflow;
    uint32_t playback_queue_overflow;
    uint32_t bad_capture_len;
    uint32_t bad_playback_len;
    uint32_t capture_left_peak;
    uint32_t capture_right_peak;

    /* UAC-STA -> AP: last 10 ms PCM block before entering the I2S queue. */
    uint32_t uac_rx_left_peak;
    uint32_t uac_rx_right_peak;
    uint32_t uac_rx_zero_blocks;
    uint32_t uac_rx_nonzero_blocks;

    /* AP -> SSD212: last 5 ms PCM block consumed by the I2S TX DMA. */
    uint32_t playback_left_peak;
    uint32_t playback_right_peak;
    uint32_t playback_zero_callbacks;
    uint32_t playback_nonzero_callbacks;

    /* Raw 32-bit I2S words seen/created by the DMA callbacks. */
    uint32_t capture_raw_nonzero_words;
    uint32_t playback_raw_nonzero_words;
} app_i2s_ssd212_bridge_stats_t;

int app_i2s_ssd212_bridge_start(void);
int app_i2s_ssd212_bridge_stop(void);
uint8_t app_i2s_ssd212_bridge_is_started(void);
void app_i2s_ssd212_bridge_get_stats(app_i2s_ssd212_bridge_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __APP_I2S_SSD212_BRIDGE_H__ */
