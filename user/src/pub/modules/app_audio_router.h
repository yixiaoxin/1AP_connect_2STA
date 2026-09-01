#ifndef __APP_AUDIO_ROUTER_H__
#define __APP_AUDIO_ROUTER_H__

#include <stdint.h>

#include "modules/app_audio_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AP multi-STA audio router.
 *
 * Input : one 16 kHz / stereo / PCM16 stream per STA.
 * Output: interleaved PCM16 channels for the SPI bridge.
 *
 * Each STA owns an independent jitter ring and mutex.  The SPI reader no
 * longer holds one global lock while reading every source.
 */

#ifdef APP_AUDIO_ROUTER_ENABLE
#undef APP_AUDIO_ROUTER_ENABLE
#endif
#define APP_AUDIO_ROUTER_ENABLE             0

#ifndef APP_AUDIO_ROUTER_ACTIVE_STA
#define APP_AUDIO_ROUTER_ACTIVE_STA         APP_AUDIO_LINK_AP_MAX_STA
#endif

#define APP_AUDIO_ROUTER_STA_CHANNELS       2U
#define APP_AUDIO_ROUTER_OUT_CHANNELS       (APP_AUDIO_ROUTER_ACTIVE_STA * APP_AUDIO_ROUTER_STA_CHANNELS)

#define APP_AUDIO_ROUTER_SAMPLE_RATE        16000U
#define APP_AUDIO_ROUTER_BYTES_PER_SAMPLE   2U
#define APP_AUDIO_ROUTER_BYTES_PER_STA_FRAME \
    (APP_AUDIO_ROUTER_STA_CHANNELS * APP_AUDIO_ROUTER_BYTES_PER_SAMPLE)

#if (APP_AUDIO_ROUTER_ACTIVE_STA < 1)
#error "APP_AUDIO_ROUTER_ACTIVE_STA must be >= 1"
#endif

#if (APP_AUDIO_ROUTER_ACTIVE_STA > APP_AUDIO_LINK_AP_MAX_STA)
#error "APP_AUDIO_ROUTER_ACTIVE_STA must be <= APP_AUDIO_LINK_AP_MAX_STA"
#endif

typedef struct {
    uint8_t  connected;
    uint8_t  muted;
    uint8_t  started;
    uint8_t  client_id;
    uint32_t level_frames;
    uint32_t min_level_frames;
    uint32_t max_level_frames;
    uint32_t rx_packets;
    uint32_t rx_frames;
    uint32_t underflow_frames;
    uint32_t dropped_frames;
    uint32_t seq_lost_packets;
    uint32_t last_seq;
    uint32_t last_rx_age_ms;
    uint64_t last_remote_timestamp_us;

    /* Stage-5 software clock-drift compensation diagnostics. */
    uint32_t drift_drop_frames;      /* input frames skipped one at a time */
    uint32_t drift_repeat_frames;    /* input frames repeated one at a time */
    uint32_t rebuffer_count;
    int32_t  last_drift_error_frames;
    int8_t   last_drift_correction;  /* +1=drop one, -1=repeat one, 0=none */
    uint8_t  reserved[3];
} app_audio_router_sta_status_t;

typedef struct {
    uint8_t  inited;
    uint8_t  enabled;
    uint8_t  connected_sta;
    uint8_t  started_sta;
    uint32_t out_frames;
    uint32_t underflow_frames;
    uint32_t dropped_frames;
    uint32_t seq_lost_packets;
    uint32_t rx_packets;
    uint32_t rx_frames;
    uint32_t drift_drop_frames;
    uint32_t drift_repeat_frames;
    uint32_t rebuffer_count;
} app_audio_router_status_t;

void app_audio_router_init(void);
void app_audio_router_reset(void);
void app_audio_router_set_enabled(uint8_t enabled);
uint8_t app_audio_router_is_enabled(void);

void app_audio_router_set_sta_connected(uint8_t client_id, uint8_t connected);
void app_audio_router_set_sta_muted(uint8_t client_id, uint8_t muted);

/* AP audio_link calls this after receiving one STA PCM packet. */
void app_audio_router_push_sta_pcm(uint8_t client_id,
                                   uint32_t seq_num,
                                   uint64_t remote_timestamp_us,
                                   uint8_t muted,
                                   const uint8_t *payload,
                                   uint32_t payload_bytes);

/*
 * Read multi-channel interleaved PCM16.
 *
 * 4 STA / 8 channel layout:
 *   STA1_L, STA1_R, STA2_L, STA2_R, STA3_L, STA3_R, STA4_L, STA4_R
 *
 * valid/mute/lost masks use output-channel bits.  Missing audio is zero-filled.
 */
uint32_t app_audio_router_read_tdm_s16(int16_t *out,
                                       uint32_t frames,
                                       uint32_t *valid_mask,
                                       uint32_t *mute_mask,
                                       uint32_t *lost_mask);

uint32_t app_audio_router_read_pcm16_interleaved(int16_t *out,
                                                  uint32_t frames,
                                                  uint32_t *valid_mask,
                                                  uint32_t *mute_mask,
                                                  uint32_t *lost_mask);

void app_audio_router_get_status(app_audio_router_status_t *status);
int app_audio_router_get_sta_status(uint8_t client_id,
                                    app_audio_router_sta_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __APP_AUDIO_ROUTER_H__ */
