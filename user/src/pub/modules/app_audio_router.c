#include "modules/app_audio_router.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "dbg.h"
#include "rtos.h"
#include "rtos_al.h"
#include "rtos_def.h"

#if APP_AUDIO_ROUTER_ENABLE

#ifndef APP_AUDIO_ROUTER_RING_FRAMES
#define APP_AUDIO_ROUTER_RING_FRAMES          4096U   /* 256 ms @ 16 kHz */
#endif

#ifndef APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES
#define APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES  800U    /* 50 ms startup target, recording-stability first */
#endif

#ifndef APP_AUDIO_ROUTER_MAX_DELAY_FRAMES
#define APP_AUDIO_ROUTER_MAX_DELAY_FRAMES     2400U   /* 150 ms emergency ceiling */
#endif

#ifndef APP_AUDIO_ROUTER_REMOTE_TIMEOUT_MS
#define APP_AUDIO_ROUTER_REMOTE_TIMEOUT_MS    3000U
#endif

#ifndef APP_AUDIO_ROUTER_STATS_LOG_ENABLE
#define APP_AUDIO_ROUTER_STATS_LOG_ENABLE     0
#endif

#ifndef APP_AUDIO_ROUTER_LOG_INTERVAL_PACKETS
#define APP_AUDIO_ROUTER_LOG_INTERVAL_PACKETS 250U
#endif

/*
 * Software clock-drift compensation.
 *
 * Occupancy is averaged over a number of 10 ms output blocks.  When the
 * average remains outside the dead band, the reader skips or repeats one
 * stereo frame near the middle of the next block.  One-frame corrections are
 * far less audible than dropping a complete 20 ms network packet.
 */
#ifndef APP_AUDIO_ROUTER_DRIFT_COMP_ENABLE
#define APP_AUDIO_ROUTER_DRIFT_COMP_ENABLE    1
#endif

#ifndef APP_AUDIO_ROUTER_DRIFT_CHECK_BLOCKS
#define APP_AUDIO_ROUTER_DRIFT_CHECK_BLOCKS   20U     /* about 200 ms */
#endif

#ifndef APP_AUDIO_ROUTER_DRIFT_DEADBAND_FRAMES
#define APP_AUDIO_ROUTER_DRIFT_DEADBAND_FRAMES 96U    /* 6 ms @ 16 kHz */
#endif

/* A short sequence gap is represented by silence in the jitter ring instead
 * of forcing a full 30 ms restart.  Large gaps still restart cleanly. */
#ifndef APP_AUDIO_ROUTER_PLC_MAX_LOST_PACKETS
#define APP_AUDIO_ROUTER_PLC_MAX_LOST_PACKETS  2U
#endif

#ifndef APP_AUDIO_ROUTER_CONCEAL_EMPTY_BLOCKS
#define APP_AUDIO_ROUTER_CONCEAL_EMPTY_BLOCKS  4U
#endif

#if (APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES >= APP_AUDIO_ROUTER_RING_FRAMES)
#error "APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES must be < APP_AUDIO_ROUTER_RING_FRAMES"
#endif

#if (APP_AUDIO_ROUTER_MAX_DELAY_FRAMES >= APP_AUDIO_ROUTER_RING_FRAMES)
#error "APP_AUDIO_ROUTER_MAX_DELAY_FRAMES must be < APP_AUDIO_ROUTER_RING_FRAMES"
#endif

#if (APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES >= APP_AUDIO_ROUTER_MAX_DELAY_FRAMES)
#error "APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES must be < APP_AUDIO_ROUTER_MAX_DELAY_FRAMES"
#endif

#if (APP_AUDIO_ROUTER_DRIFT_CHECK_BLOCKS < 1U)
#error "APP_AUDIO_ROUTER_DRIFT_CHECK_BLOCKS must be >= 1"
#endif

typedef struct {
    uint8_t  connected;
    uint8_t  muted;
    uint8_t  started;
    uint8_t  seq_inited;

    uint32_t wr;
    uint32_t rd;
    uint32_t level;
    uint32_t min_level;
    uint32_t max_level;
    uint8_t  level_stat_valid;

    uint32_t last_seq;
    uint32_t rx_packets;
    uint32_t rx_frames;
    uint32_t dropped_frames;
    uint32_t underflow_frames;
    uint32_t seq_lost;

    uint32_t last_rx_ms;
    uint64_t last_remote_timestamp_us;

    uint32_t drift_level_sum;
    uint32_t drift_block_count;
    uint32_t drift_drop_frames;
    uint32_t drift_repeat_frames;
    uint32_t rebuffer_count;
    int32_t  last_drift_error_frames;
    int8_t   last_drift_correction;
    uint8_t  conceal_empty_blocks;
    uint8_t  last_output_valid;
    uint8_t  reserved;
    int16_t  last_output[APP_AUDIO_ROUTER_STA_CHANNELS];

    int16_t ring[APP_AUDIO_ROUTER_RING_FRAMES * APP_AUDIO_ROUTER_STA_CHANNELS];
} app_audio_router_sta_t;

static volatile uint8_t s_router_inited = 0;
static volatile uint8_t s_router_enabled = 0;
static uint32_t s_out_frames = 0;
static uint32_t s_out_packets = 0;
static app_audio_router_sta_t s_sta[APP_AUDIO_ROUTER_ACTIVE_STA];
static rtos_mutex s_sta_mutex[APP_AUDIO_ROUTER_ACTIVE_STA];

static uint32_t app_audio_router_time_ms(void)
{
    return (uint32_t)rtos_now(false);
}

static int app_audio_router_id_to_index(uint8_t client_id)
{
    if ((client_id == 0U) || (client_id > APP_AUDIO_ROUTER_ACTIVE_STA)) {
        return -1;
    }
    return (int)client_id - 1;
}

static void app_audio_router_lock_sta(uint32_t idx)
{
    if ((idx < APP_AUDIO_ROUTER_ACTIVE_STA) && (s_sta_mutex[idx] != NULL)) {
        rtos_mutex_lock(s_sta_mutex[idx], -1);
    }
}

static void app_audio_router_unlock_sta(uint32_t idx)
{
    if ((idx < APP_AUDIO_ROUTER_ACTIVE_STA) && (s_sta_mutex[idx] != NULL)) {
        rtos_mutex_unlock(s_sta_mutex[idx]);
    }
}

static uint32_t app_audio_router_add_index(uint32_t idx, uint32_t add)
{
    idx += add;
    while (idx >= APP_AUDIO_ROUTER_RING_FRAMES) {
        idx -= APP_AUDIO_ROUTER_RING_FRAMES;
    }
    return idx;
}

static int16_t app_audio_router_le16_to_s16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void app_audio_router_reset_drift_window(app_audio_router_sta_t *r)
{
    r->drift_level_sum = 0U;
    r->drift_block_count = 0U;
    r->last_drift_error_frames = 0;
    r->last_drift_correction = 0;
}

static void app_audio_router_sta_clear(app_audio_router_sta_t *r)
{
    if (r == NULL) {
        return;
    }

    memset(r, 0, sizeof(*r));
}

static void app_audio_router_update_level_stat(app_audio_router_sta_t *r)
{
    if (!r->level_stat_valid) {
        r->min_level = r->level;
        r->max_level = r->level;
        r->level_stat_valid = 1U;
        return;
    }

    if (r->level < r->min_level) {
        r->min_level = r->level;
    }
    if (r->level > r->max_level) {
        r->max_level = r->level;
    }
}

static void app_audio_router_drop_frames(app_audio_router_sta_t *r,
                                         uint32_t frames,
                                         uint8_t count_as_drop)
{
    if (frames > r->level) {
        frames = r->level;
    }
    if (frames == 0U) {
        return;
    }

    r->rd = app_audio_router_add_index(r->rd, frames);
    r->level -= frames;
    if (count_as_drop) {
        r->dropped_frames += frames;
    }
    app_audio_router_update_level_stat(r);
}

static void app_audio_router_emergency_trim(app_audio_router_sta_t *r)
{
    if (r->level > APP_AUDIO_ROUTER_MAX_DELAY_FRAMES) {
        /* Recording stability is more important than ultra-low latency here.
         * Do not cut the jitter buffer all the way back to target in one step;
         * keep it near the high-water area so one Wi-Fi burst does not become
         * an audible 40~100 ms hole. */
        uint32_t keep = APP_AUDIO_ROUTER_MAX_DELAY_FRAMES -
                        (APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES / 2U);
        uint32_t drop;

        if (keep < APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES) {
            keep = APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES;
        }
        if (keep >= APP_AUDIO_ROUTER_RING_FRAMES) {
            keep = APP_AUDIO_ROUTER_RING_FRAMES - 1U;
        }

        drop = r->level - keep;
        app_audio_router_drop_frames(r, drop, 1U);
        /* Keep output running after trimming stale latency. */
        app_audio_router_reset_drift_window(r);
    }
}

static void app_audio_router_write_batch(app_audio_router_sta_t *r,
                                         const uint8_t *payload,
                                         uint32_t start_frame,
                                         uint32_t frames)
{
    uint32_t i;
    uint32_t required;

    if ((r == NULL) || (payload == NULL) || (frames == 0U)) {
        return;
    }

    required = r->level + frames;
    if (required >= APP_AUDIO_ROUTER_RING_FRAMES) {
        uint32_t usable = APP_AUDIO_ROUTER_RING_FRAMES - 1U;
        uint32_t drop = required - usable;
        app_audio_router_drop_frames(r, drop, 1U);
    }

    for (i = 0U; i < frames; i++) {
        uint32_t src_frame = start_frame + i;
        uint32_t src = src_frame * APP_AUDIO_ROUTER_BYTES_PER_STA_FRAME;
        uint32_t dst_frame = app_audio_router_add_index(r->wr, i);
        uint32_t dst = dst_frame * APP_AUDIO_ROUTER_STA_CHANNELS;

        r->ring[dst + 0U] = app_audio_router_le16_to_s16(&payload[src + 0U]);
        r->ring[dst + 1U] = app_audio_router_le16_to_s16(&payload[src + 2U]);
    }

    r->wr = app_audio_router_add_index(r->wr, frames);
    r->level += frames;
    r->rx_frames += frames;
    app_audio_router_update_level_stat(r);
    app_audio_router_emergency_trim(r);
}

static void app_audio_router_write_silence(app_audio_router_sta_t *r,
                                           uint32_t frames)
{
    uint32_t i;
    uint32_t required;

    if ((r == NULL) || (frames == 0U)) {
        return;
    }

    required = r->level + frames;
    if (required >= APP_AUDIO_ROUTER_RING_FRAMES) {
        uint32_t usable = APP_AUDIO_ROUTER_RING_FRAMES - 1U;
        uint32_t drop = required - usable;
        app_audio_router_drop_frames(r, drop, 1U);
    }

    for (i = 0U; i < frames; i++) {
        uint32_t dst_frame = app_audio_router_add_index(r->wr, i);
        uint32_t dst = dst_frame * APP_AUDIO_ROUTER_STA_CHANNELS;
        r->ring[dst] = 0;
        r->ring[dst + 1U] = 0;
    }
    r->wr = app_audio_router_add_index(r->wr, frames);
    r->level += frames;
    app_audio_router_update_level_stat(r);
    app_audio_router_emergency_trim(r);
}

static int8_t app_audio_router_choose_drift_correction(app_audio_router_sta_t *r,
                                                        uint32_t out_frames)
{
#if APP_AUDIO_ROUTER_DRIFT_COMP_ENABLE
    uint32_t average;
    uint32_t target_center;
    int32_t error;

    r->drift_level_sum += r->level;
    r->drift_block_count++;
    r->last_drift_correction = 0;

    if (r->drift_block_count < APP_AUDIO_ROUTER_DRIFT_CHECK_BLOCKS) {
        return 0;
    }

    average = r->drift_level_sum / r->drift_block_count;
    target_center = APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES + (out_frames / 2U);
    error = (int32_t)average - (int32_t)target_center;
    r->last_drift_error_frames = error;
    r->drift_level_sum = 0U;
    r->drift_block_count = 0U;

    if ((error > (int32_t)APP_AUDIO_ROUTER_DRIFT_DEADBAND_FRAMES) &&
        (r->level >= (out_frames + 1U))) {
        r->last_drift_correction = 1;
        return 1;   /* consume one extra input frame */
    }

    if ((error < -(int32_t)APP_AUDIO_ROUTER_DRIFT_DEADBAND_FRAMES) &&
        (out_frames > 1U) &&
        (r->level >= (out_frames - 1U))) {
        r->last_drift_correction = -1;
        return -1;  /* consume one fewer input frame */
    }
#else
    (void)r;
    (void)out_frames;
#endif
    return 0;
}

static uint8_t app_audio_router_read_sta_block(app_audio_router_sta_t *r,
                                                int16_t *out,
                                                uint32_t out_frames,
                                                uint32_t out_stride,
                                                uint8_t *muted,
                                                uint8_t *lost)
{
    uint32_t f;
    uint32_t consume_frames;
    uint32_t slip_pos;
    int8_t correction;

    *muted = 0U;
    *lost = 0U;

    if (!r->connected) {
        return 0U;
    }
    if (r->muted) {
        *muted = 1U;
        return 0U;
    }

    if (!r->started) {
        if (r->level >= APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES) {
            r->started = 1U;
            r->conceal_empty_blocks = 0U;
            app_audio_router_reset_drift_window(r);
        } else {
            r->underflow_frames += out_frames;
            *lost = 1U;
            return 0U;
        }
    }

    if (r->level < out_frames) {
        uint32_t available = r->level;

        /* Recording-stability mode: if we already have at least half a block,
         * output the real samples first and only conceal the missing tail.
         * The old low-latency behavior discarded any partial block, which made
         * short Wi-Fi jitter more audible. */
        if (available >= (out_frames / 2U)) {
            int32_t last_l = r->last_output_valid ? r->last_output[0] : 0;
            int32_t last_r = r->last_output_valid ? r->last_output[1] : 0;
            uint32_t tail_frames = out_frames - available;

            for (f = 0U; f < available; f++) {
                uint32_t src_frame = app_audio_router_add_index(r->rd, f);
                uint32_t src = src_frame * APP_AUDIO_ROUTER_STA_CHANNELS;
                out[f * out_stride] = r->ring[src];
                out[f * out_stride + 1U] = r->ring[src + 1U];
                last_l = out[f * out_stride];
                last_r = out[f * out_stride + 1U];
            }

            for (; f < out_frames; f++) {
                uint32_t tail_pos = f - available;
                uint32_t remain = tail_frames - tail_pos;
                out[f * out_stride] = (int16_t)((last_l * (int32_t)remain) /
                                                (int32_t)(tail_frames + 1U));
                out[f * out_stride + 1U] = (int16_t)((last_r * (int32_t)remain) /
                                                     (int32_t)(tail_frames + 1U));
            }

            r->rd = app_audio_router_add_index(r->rd, available);
            r->level = 0U;
            r->underflow_frames += tail_frames;
            r->conceal_empty_blocks = 0U;
            r->last_output[0] = out[(out_frames - 1U) * out_stride];
            r->last_output[1] = out[(out_frames - 1U) * out_stride + 1U];
            r->last_output_valid = 1U;
            *lost = 1U;
            app_audio_router_update_level_stat(r);
            return 1U;
        }

        /* Too little data remains to be useful; discard the tiny residual
         * before PLC so it is not replayed late after the next packet arrives. */
        if ((r->conceal_empty_blocks == 0U) && (r->level > 0U)) {
            app_audio_router_drop_frames(r, r->level, 0U);
        }
        r->underflow_frames += out_frames;
        *lost = 1U;
        if (r->conceal_empty_blocks < 0xFFU) {
            r->conceal_empty_blocks++;
        }

        if (r->conceal_empty_blocks <= APP_AUDIO_ROUTER_CONCEAL_EMPTY_BLOCKS) {
            int32_t last_l = r->last_output_valid ? r->last_output[0] : 0;
            int32_t last_r = r->last_output_valid ? r->last_output[1] : 0;
            for (f = 0U; f < out_frames; f++) {
                uint32_t remain = out_frames - 1U - f;
                if (r->conceal_empty_blocks == 1U) {
                    out[f * out_stride] = (int16_t)((last_l * (int32_t)remain) /
                                                    (int32_t)out_frames);
                    out[f * out_stride + 1U] = (int16_t)((last_r * (int32_t)remain) /
                                                         (int32_t)out_frames);
                } else {
                    out[f * out_stride] = 0;
                    out[f * out_stride + 1U] = 0;
                }
            }
            r->last_output[0] = 0;
            r->last_output[1] = 0;
            r->last_output_valid = 1U;
            return 1U;
        }

        r->started = 0U;
        r->rebuffer_count++;
        r->conceal_empty_blocks = 0U;
        r->last_output_valid = 0U;
        app_audio_router_reset_drift_window(r);
        return 0U;
    }

    r->conceal_empty_blocks = 0U;
    correction = app_audio_router_choose_drift_correction(r, out_frames);
    consume_frames = (uint32_t)((int32_t)out_frames + (int32_t)correction);
    slip_pos = out_frames / 2U;

    for (f = 0U; f < out_frames; f++) {
        uint32_t src_offset = f;
        uint32_t src_frame;
        uint32_t src;

        if ((correction > 0) && (f >= slip_pos)) {
            src_offset = f + 1U;
        } else if ((correction < 0) && (f > slip_pos)) {
            src_offset = f - 1U;
        }

        src_frame = app_audio_router_add_index(r->rd, src_offset);
        src = src_frame * APP_AUDIO_ROUTER_STA_CHANNELS;
        out[f * out_stride] = r->ring[src];
        out[f * out_stride + 1U] = r->ring[src + 1U];
    }

    r->last_output[0] = out[(out_frames - 1U) * out_stride];
    r->last_output[1] = out[(out_frames - 1U) * out_stride + 1U];
    r->last_output_valid = 1U;
    r->rd = app_audio_router_add_index(r->rd, consume_frames);
    r->level -= consume_frames;
    if (correction > 0) {
        r->drift_drop_frames++;
    } else if (correction < 0) {
        r->drift_repeat_frames++;
    }
    app_audio_router_update_level_stat(r);
    return 1U;
}

void app_audio_router_init(void)
{
    uint32_t i;

    if (s_router_inited) {
        return;
    }

    for (i = 0U; i < APP_AUDIO_ROUTER_ACTIVE_STA; i++) {
        if (s_sta_mutex[i] == NULL) {
            (void)rtos_mutex_create(&s_sta_mutex[i]);
        }
        app_audio_router_sta_clear(&s_sta[i]);
    }

    s_out_frames = 0U;
    s_out_packets = 0U;
    s_router_enabled = 1U;
    s_router_inited = 1U;

    dbg("AUDIO_ROUTER: init sta=%u ch=%u ring=%u target=%u max=%u drift=%u\r\n",
        (unsigned int)APP_AUDIO_ROUTER_ACTIVE_STA,
        (unsigned int)APP_AUDIO_ROUTER_OUT_CHANNELS,
        (unsigned int)APP_AUDIO_ROUTER_RING_FRAMES,
        (unsigned int)APP_AUDIO_ROUTER_TARGET_DELAY_FRAMES,
        (unsigned int)APP_AUDIO_ROUTER_MAX_DELAY_FRAMES,
        (unsigned int)APP_AUDIO_ROUTER_DRIFT_COMP_ENABLE);
}

void app_audio_router_reset(void)
{
    uint32_t i;

    if (!s_router_inited) {
        app_audio_router_init();
        return;
    }

    for (i = 0U; i < APP_AUDIO_ROUTER_ACTIVE_STA; i++) {
        app_audio_router_lock_sta(i);
        app_audio_router_sta_clear(&s_sta[i]);
        app_audio_router_unlock_sta(i);
    }
    s_out_frames = 0U;
    s_out_packets = 0U;
}

void app_audio_router_set_enabled(uint8_t enabled)
{
    if (!s_router_inited) {
        app_audio_router_init();
    }

    s_router_enabled = enabled ? 1U : 0U;
    if (!s_router_enabled) {
        app_audio_router_reset();
    }
}

uint8_t app_audio_router_is_enabled(void)
{
    return s_router_enabled ? 1U : 0U;
}

void app_audio_router_set_sta_connected(uint8_t client_id, uint8_t connected)
{
    int idx;

    if (!s_router_inited) {
        app_audio_router_init();
    }

    idx = app_audio_router_id_to_index(client_id);
    if (idx < 0) {
        return;
    }

    app_audio_router_lock_sta((uint32_t)idx);
    app_audio_router_sta_clear(&s_sta[idx]);
    if (connected) {
        s_sta[idx].connected = 1U;
        s_sta[idx].last_rx_ms = app_audio_router_time_ms();
    }
    app_audio_router_unlock_sta((uint32_t)idx);

    dbg("AUDIO_ROUTER: STA%u connected=%u\r\n",
        (unsigned int)client_id,
        (unsigned int)(connected ? 1U : 0U));
}

void app_audio_router_set_sta_muted(uint8_t client_id, uint8_t muted)
{
    int idx;

    if (!s_router_inited) {
        app_audio_router_init();
    }

    idx = app_audio_router_id_to_index(client_id);
    if (idx < 0) {
        return;
    }

    app_audio_router_lock_sta((uint32_t)idx);
    s_sta[idx].muted = muted ? 1U : 0U;
    if (s_sta[idx].muted) {
        s_sta[idx].started = 0U;
        s_sta[idx].rd = s_sta[idx].wr;
        s_sta[idx].level = 0U;
        s_sta[idx].conceal_empty_blocks = 0U;
        s_sta[idx].last_output_valid = 0U;
        app_audio_router_reset_drift_window(&s_sta[idx]);
        app_audio_router_update_level_stat(&s_sta[idx]);
    }
    app_audio_router_unlock_sta((uint32_t)idx);
}

void app_audio_router_push_sta_pcm(uint8_t client_id,
                                   uint32_t seq_num,
                                   uint64_t remote_timestamp_us,
                                   uint8_t muted,
                                   const uint8_t *payload,
                                   uint32_t payload_bytes)
{
    int idx;
    app_audio_router_sta_t *r;
    uint32_t frames;
    uint32_t start_frame = 0U;

    if (!s_router_inited) {
        app_audio_router_init();
    }

    if (!s_router_enabled || (payload == NULL) ||
        (payload_bytes < APP_AUDIO_ROUTER_BYTES_PER_STA_FRAME)) {
        return;
    }

    idx = app_audio_router_id_to_index(client_id);
    if (idx < 0) {
        return;
    }

    frames = payload_bytes / APP_AUDIO_ROUTER_BYTES_PER_STA_FRAME;
    if (frames >= APP_AUDIO_ROUTER_RING_FRAMES) {
        frames = APP_AUDIO_ROUTER_RING_FRAMES / 2U;
        start_frame = (payload_bytes / APP_AUDIO_ROUTER_BYTES_PER_STA_FRAME) - frames;
    }

    app_audio_router_lock_sta((uint32_t)idx);
    r = &s_sta[idx];
    if (!r->connected) {
        r->connected = 1U;
    }

    r->muted = muted ? 1U : 0U;
    r->last_rx_ms = app_audio_router_time_ms();
    r->last_remote_timestamp_us = remote_timestamp_us;
    r->rx_packets++;

    if (!r->seq_inited) {
        r->seq_inited = 1U;
        r->last_seq = seq_num;
    } else {
        uint32_t expected = r->last_seq + 1U;
        if (seq_num != expected) {
            uint32_t diff = seq_num - expected;
            if (diff < 0x80000000U) {
                r->seq_lost += diff;
                if (!r->muted && (diff <= APP_AUDIO_ROUTER_PLC_MAX_LOST_PACKETS)) {
                    uint64_t missing = (uint64_t)diff * (uint64_t)frames;
                    if (missing < APP_AUDIO_ROUTER_RING_FRAMES) {
                        app_audio_router_write_silence(r, (uint32_t)missing);
                    }
                } else {
                    /* A long outage is not useful history; restart from the new packet. */
                    r->rd = r->wr;
                    r->level = 0U;
                    r->started = 0U;
                    r->conceal_empty_blocks = 0U;
                    r->last_output_valid = 0U;
                    r->rebuffer_count++;
                    app_audio_router_reset_drift_window(r);
                }
            }
        }
        r->last_seq = seq_num;
    }

    if (!r->muted) {
        app_audio_router_write_batch(r, payload, start_frame, frames);
    } else {
        r->started = 0U;
        r->rd = r->wr;
        r->level = 0U;
        r->conceal_empty_blocks = 0U;
        r->last_output_valid = 0U;
        app_audio_router_reset_drift_window(r);
        app_audio_router_update_level_stat(r);
    }

#if APP_AUDIO_ROUTER_STATS_LOG_ENABLE
    if ((r->rx_packets % APP_AUDIO_ROUTER_LOG_INTERVAL_PACKETS) == 0U) {
        dbg("AUDIO_ROUTER: STA%u rx=%u level=%u drop=%u under=%u seq_lost=%u drift_drop=%u drift_repeat=%u\r\n",
            (unsigned int)client_id,
            (unsigned int)r->rx_packets,
            (unsigned int)r->level,
            (unsigned int)r->dropped_frames,
            (unsigned int)r->underflow_frames,
            (unsigned int)r->seq_lost,
            (unsigned int)r->drift_drop_frames,
            (unsigned int)r->drift_repeat_frames);
    }
#endif

    app_audio_router_unlock_sta((uint32_t)idx);
}

uint32_t app_audio_router_read_tdm_s16(int16_t *out,
                                       uint32_t frames,
                                       uint32_t *valid_mask,
                                       uint32_t *mute_mask,
                                       uint32_t *lost_mask)
{
    uint32_t i;
    uint32_t now_ms;
    uint32_t vmask = 0U;
    uint32_t mmask = 0U;
    uint32_t lmask = 0U;

    if ((out == NULL) || (frames == 0U)) {
        if (valid_mask != NULL) *valid_mask = 0U;
        if (mute_mask != NULL) *mute_mask = 0U;
        if (lost_mask != NULL) *lost_mask = 0U;
        return 0U;
    }

    if (!s_router_inited) {
        app_audio_router_init();
    }

    memset(out, 0, frames * APP_AUDIO_ROUTER_OUT_CHANNELS * sizeof(int16_t));
    if (!s_router_enabled) {
        if (valid_mask != NULL) *valid_mask = 0U;
        if (mute_mask != NULL) *mute_mask = 0U;
        if (lost_mask != NULL) *lost_mask = 0U;
        return frames;
    }

    now_ms = app_audio_router_time_ms();

    /* Lock and process one STA at a time.  TCP writes to other STA rings remain free. */
    for (i = 0U; i < APP_AUDIO_ROUTER_ACTIVE_STA; i++) {
        app_audio_router_sta_t *r = &s_sta[i];
        uint8_t valid;
        uint8_t muted;
        uint8_t lost;
        uint32_t slot_l = i * APP_AUDIO_ROUTER_STA_CHANNELS;
        uint32_t slot_r = slot_l + 1U;

        app_audio_router_lock_sta(i);

        if (r->connected && (r->last_rx_ms != 0U) &&
            ((now_ms - r->last_rx_ms) > APP_AUDIO_ROUTER_REMOTE_TIMEOUT_MS)) {
            app_audio_router_sta_clear(r);
        }

        valid = app_audio_router_read_sta_block(r,
                                                &out[slot_l],
                                                frames,
                                                APP_AUDIO_ROUTER_OUT_CHANNELS,
                                                &muted,
                                                &lost);
        if (valid) {
            vmask |= (1UL << slot_l) | (1UL << slot_r);
        }
        if (muted) {
            mmask |= (1UL << slot_l) | (1UL << slot_r);
        }
        if (lost) {
            lmask |= (1UL << slot_l) | (1UL << slot_r);
        }

        app_audio_router_unlock_sta(i);
    }

    s_out_packets++;
    s_out_frames += frames;

    if (valid_mask != NULL) *valid_mask = vmask;
    if (mute_mask != NULL) *mute_mask = mmask;
    if (lost_mask != NULL) *lost_mask = lmask;
    return frames;
}

uint32_t app_audio_router_read_pcm16_interleaved(int16_t *out,
                                                  uint32_t frames,
                                                  uint32_t *valid_mask,
                                                  uint32_t *mute_mask,
                                                  uint32_t *lost_mask)
{
    return app_audio_router_read_tdm_s16(out, frames, valid_mask, mute_mask, lost_mask);
}

void app_audio_router_get_status(app_audio_router_status_t *status)
{
    uint32_t i;

    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->inited = s_router_inited ? 1U : 0U;
    status->enabled = s_router_enabled ? 1U : 0U;
    status->out_frames = s_out_frames;

    if (!s_router_inited) {
        return;
    }

    for (i = 0U; i < APP_AUDIO_ROUTER_ACTIVE_STA; i++) {
        app_audio_router_lock_sta(i);
        if (s_sta[i].connected) status->connected_sta++;
        if (s_sta[i].started) status->started_sta++;
        status->underflow_frames += s_sta[i].underflow_frames;
        status->dropped_frames += s_sta[i].dropped_frames;
        status->seq_lost_packets += s_sta[i].seq_lost;
        status->rx_packets += s_sta[i].rx_packets;
        status->rx_frames += s_sta[i].rx_frames;
        status->drift_drop_frames += s_sta[i].drift_drop_frames;
        status->drift_repeat_frames += s_sta[i].drift_repeat_frames;
        status->rebuffer_count += s_sta[i].rebuffer_count;
        app_audio_router_unlock_sta(i);
    }
}

int app_audio_router_get_sta_status(uint8_t client_id,
                                    app_audio_router_sta_status_t *status)
{
    int idx;
    uint32_t now_ms;
    app_audio_router_sta_t *r;

    if (status == NULL) {
        return -1;
    }

    memset(status, 0, sizeof(*status));
    idx = app_audio_router_id_to_index(client_id);
    if (idx < 0) {
        return -2;
    }

    now_ms = app_audio_router_time_ms();
    app_audio_router_lock_sta((uint32_t)idx);
    r = &s_sta[idx];

    status->connected = r->connected;
    status->muted = r->muted;
    status->started = r->started;
    status->client_id = client_id;
    status->level_frames = r->level;
    status->min_level_frames = r->level_stat_valid ? r->min_level : r->level;
    status->max_level_frames = r->level_stat_valid ? r->max_level : r->level;
    status->rx_packets = r->rx_packets;
    status->rx_frames = r->rx_frames;
    status->underflow_frames = r->underflow_frames;
    status->dropped_frames = r->dropped_frames;
    status->seq_lost_packets = r->seq_lost;
    status->last_seq = r->last_seq;
    status->last_remote_timestamp_us = r->last_remote_timestamp_us;
    status->drift_drop_frames = r->drift_drop_frames;
    status->drift_repeat_frames = r->drift_repeat_frames;
    status->rebuffer_count = r->rebuffer_count;
    status->last_drift_error_frames = r->last_drift_error_frames;
    status->last_drift_correction = r->last_drift_correction;
    if ((r->last_rx_ms != 0U) && (now_ms >= r->last_rx_ms)) {
        status->last_rx_age_ms = now_ms - r->last_rx_ms;
    }

    app_audio_router_unlock_sta((uint32_t)idx);
    return 0;
}

#else /* APP_AUDIO_ROUTER_ENABLE */

void app_audio_router_init(void) {}
void app_audio_router_reset(void) {}
void app_audio_router_set_enabled(uint8_t enabled) { (void)enabled; }
uint8_t app_audio_router_is_enabled(void) { return 0U; }
void app_audio_router_set_sta_connected(uint8_t client_id, uint8_t connected)
{ (void)client_id; (void)connected; }
void app_audio_router_set_sta_muted(uint8_t client_id, uint8_t muted)
{ (void)client_id; (void)muted; }
void app_audio_router_push_sta_pcm(uint8_t client_id, uint32_t seq_num,
                                   uint64_t remote_timestamp_us, uint8_t muted,
                                   const uint8_t *payload, uint32_t payload_bytes)
{
    (void)client_id; (void)seq_num; (void)remote_timestamp_us;
    (void)muted; (void)payload; (void)payload_bytes;
}
uint32_t app_audio_router_read_tdm_s16(int16_t *out, uint32_t frames,
                                       uint32_t *valid_mask, uint32_t *mute_mask,
                                       uint32_t *lost_mask)
{
    if (out != NULL) memset(out, 0, frames * APP_AUDIO_ROUTER_OUT_CHANNELS * sizeof(int16_t));
    if (valid_mask != NULL) *valid_mask = 0U;
    if (mute_mask != NULL) *mute_mask = 0U;
    if (lost_mask != NULL) *lost_mask = 0U;
    return frames;
}
uint32_t app_audio_router_read_pcm16_interleaved(int16_t *out, uint32_t frames,
                                                  uint32_t *valid_mask,
                                                  uint32_t *mute_mask,
                                                  uint32_t *lost_mask)
{
    return app_audio_router_read_tdm_s16(out, frames, valid_mask, mute_mask, lost_mask);
}
void app_audio_router_get_status(app_audio_router_status_t *status)
{ if (status != NULL) memset(status, 0, sizeof(*status)); }
int app_audio_router_get_sta_status(uint8_t client_id, app_audio_router_sta_status_t *status)
{
    (void)client_id;
    if (status != NULL) memset(status, 0, sizeof(*status));
    return -1;
}

#endif /* APP_AUDIO_ROUTER_ENABLE */
