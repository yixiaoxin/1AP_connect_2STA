#include "modules/app_audio_pcm.h"

#include <stdint.h>
#include <string.h>

#include "dbg.h"

/*
 * 16 kHz / stereo / PCM16 ring。
 *
 * 本版本把原来的“逐帧写、逐帧读”改成批量 memcpy，并使用单调递增的
 * frame counter 发布读写位置：
 *   - producer 先拷贝数据，最后一次性发布 wr_total；
 *   - 每个 consumer 独立维护 rd_total；
 *   - producer 不再逐帧扫描/推进所有 reader；reader 若落后超过 ring 容量，
 *     在下一次读取时自行追到仍然有效的最旧位置并计入 dropped_samples。
 *
 * 这样 I2S callback 不需要每写一帧进入一次临界区，也不会在数据尚未写完时
 * 提前发布写指针。
 */

#define APP_AUDIO_PCM_RING_FRAMES           6144U
#define APP_AUDIO_PCM_CHANNELS              2U
#define APP_AUDIO_PCM_RING_SAMPLES          (APP_AUDIO_PCM_RING_FRAMES * APP_AUDIO_PCM_CHANNELS)

#define APP_AUDIO_PCM_PACK_LOW24_LE         1

#ifndef APP_AUDIO_PCM_24_TO_16_SHIFT
#define APP_AUDIO_PCM_24_TO_16_SHIFT        8
#endif

#define APP_AUDIO_PCM_MAX_FRAMES_PER_CALL   512U

#if defined(__GNUC__)
#define APP_AUDIO_PCM_MEMORY_BARRIER()      __asm volatile ("" ::: "memory")
#else
#define APP_AUDIO_PCM_MEMORY_BARRIER()      do { } while (0)
#endif

static int16_t s_pcm_ring[APP_AUDIO_PCM_RING_SAMPLES];

/* 单调递增 frame counter；允许 uint32_t 自然回绕，差值使用无符号减法。 */
static volatile uint32_t s_pcm_wr_total = 0;
static volatile uint32_t s_pcm_rd_total[APP_AUDIO_PCM_READER_COUNT];

/* 以下统计按 int16_t sample 计数。 */
static volatile uint32_t s_pcm_written_samples = 0;
static volatile uint32_t s_pcm_reader_dropped_samples[APP_AUDIO_PCM_READER_COUNT];
static volatile uint32_t s_pcm_reader_read_samples[APP_AUDIO_PCM_READER_COUNT];
static volatile uint32_t s_pcm_reader_max_level_samples[APP_AUDIO_PCM_READER_COUNT];

/* 转换临时区静态分配，避免 ASIO callback 使用大栈。 */
static int16_t s_pcm_tmp[APP_AUDIO_PCM_MAX_FRAMES_PER_CALL * APP_AUDIO_PCM_CHANNELS];

static uint8_t app_audio_pcm_reader_valid(app_audio_pcm_reader_t reader)
{
    return ((uint32_t)reader < (uint32_t)APP_AUDIO_PCM_READER_COUNT) ? 1U : 0U;
}

static inline uint32_t app_audio_pcm_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static uint32_t app_audio_pcm_level_frames_snapshot(uint32_t wr, uint32_t rd)
{
    uint32_t level = wr - rd;

    if (level > APP_AUDIO_PCM_RING_FRAMES) {
        level = APP_AUDIO_PCM_RING_FRAMES;
    }

    return level;
}

static uint32_t app_audio_pcm_level_frames_for_reader(app_audio_pcm_reader_t reader)
{
    uint32_t wr;
    uint32_t rd;

    if (!app_audio_pcm_reader_valid(reader)) {
        return 0;
    }

    wr = s_pcm_wr_total;
    APP_AUDIO_PCM_MEMORY_BARRIER();
    rd = s_pcm_rd_total[reader];

    return app_audio_pcm_level_frames_snapshot(wr, rd);
}

static uint32_t app_audio_pcm_max_level_frames(void)
{
    uint32_t max_level = 0;
    uint32_t i;

    for (i = 0; i < (uint32_t)APP_AUDIO_PCM_READER_COUNT; i++) {
        uint32_t level = app_audio_pcm_level_frames_for_reader((app_audio_pcm_reader_t)i);

        if (level > max_level) {
            max_level = level;
        }
    }

    return max_level;
}

static uint32_t app_audio_pcm_free_frames(void)
{
    uint32_t level = app_audio_pcm_max_level_frames();

    return (level >= APP_AUDIO_PCM_RING_FRAMES) ? 0U :
           (APP_AUDIO_PCM_RING_FRAMES - level);
}

static void app_audio_pcm_copy_into_ring(uint32_t wr_total,
                                         const int16_t *src,
                                         uint32_t frames)
{
    uint32_t wr_idx = wr_total % APP_AUDIO_PCM_RING_FRAMES;
    uint32_t first_frames = app_audio_pcm_min_u32(frames,
                                                  APP_AUDIO_PCM_RING_FRAMES - wr_idx);
    uint32_t second_frames = frames - first_frames;

    memcpy(&s_pcm_ring[wr_idx * APP_AUDIO_PCM_CHANNELS],
           src,
           first_frames * APP_AUDIO_PCM_CHANNELS * sizeof(int16_t));

    if (second_frames > 0U) {
        memcpy(&s_pcm_ring[0],
               src + first_frames * APP_AUDIO_PCM_CHANNELS,
               second_frames * APP_AUDIO_PCM_CHANNELS * sizeof(int16_t));
    }
}

static void app_audio_pcm_copy_from_ring(uint32_t rd_total,
                                         int16_t *dst,
                                         uint32_t frames)
{
    uint32_t rd_idx = rd_total % APP_AUDIO_PCM_RING_FRAMES;
    uint32_t first_frames = app_audio_pcm_min_u32(frames,
                                                  APP_AUDIO_PCM_RING_FRAMES - rd_idx);
    uint32_t second_frames = frames - first_frames;

    memcpy(dst,
           &s_pcm_ring[rd_idx * APP_AUDIO_PCM_CHANNELS],
           first_frames * APP_AUDIO_PCM_CHANNELS * sizeof(int16_t));

    if (second_frames > 0U) {
        memcpy(dst + first_frames * APP_AUDIO_PCM_CHANNELS,
               &s_pcm_ring[0],
               second_frames * APP_AUDIO_PCM_CHANNELS * sizeof(int16_t));
    }
}

uint32_t app_audio_pcm_level_for(app_audio_pcm_reader_t reader)
{
    return app_audio_pcm_level_frames_for_reader(reader) * APP_AUDIO_PCM_CHANNELS;
}

uint32_t app_audio_pcm_level(void)
{
    return app_audio_pcm_level_for(APP_AUDIO_PCM_READER_WIFI);
}

static inline int32_t app_audio_pcm_s24_to_s32(const uint8_t *p)
{
#if APP_AUDIO_PCM_PACK_LOW24_LE
    uint32_t u;

    u = ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16);

    if (u & 0x00800000U) {
        u |= 0xFF000000U;
    }

    return (int32_t)u;
#else
    uint32_t raw;

    raw = ((uint32_t)p[0]) |
          ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);

    return ((int32_t)raw) >> 8;
#endif
}

static inline int16_t app_audio_pcm_clip_s16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static inline uint32_t app_audio_pcm_abs_s32(int32_t v)
{
    int64_t a = (int64_t)v;

    if (a < 0) {
        a = -a;
    }

    return (uint32_t)a;
}

void app_audio_pcm_init(void)
{
    app_audio_pcm_reset();

    dbg("AUDIO_PCM: init stereo, ring=%u frames/%u samples, readers=%u, batch IO, shift=%d\r\n",
        (unsigned int)APP_AUDIO_PCM_RING_FRAMES,
        (unsigned int)APP_AUDIO_PCM_RING_SAMPLES,
        (unsigned int)APP_AUDIO_PCM_READER_COUNT,
        APP_AUDIO_PCM_24_TO_16_SHIFT);
}

void app_audio_pcm_reset(void)
{
    uint32_t i;

    memset(s_pcm_ring, 0, sizeof(s_pcm_ring));
    memset(s_pcm_tmp, 0, sizeof(s_pcm_tmp));

    s_pcm_wr_total = 0;
    for (i = 0; i < (uint32_t)APP_AUDIO_PCM_READER_COUNT; i++) {
        s_pcm_rd_total[i] = 0;
        s_pcm_reader_dropped_samples[i] = 0;
        s_pcm_reader_read_samples[i] = 0;
        s_pcm_reader_max_level_samples[i] = 0;
    }
    s_pcm_written_samples = 0;
    APP_AUDIO_PCM_MEMORY_BARRIER();
}

void app_audio_pcm_reader_reset(app_audio_pcm_reader_t reader)
{
    uint32_t wr;

    if (!app_audio_pcm_reader_valid(reader)) {
        return;
    }

    wr = s_pcm_wr_total;
    s_pcm_rd_total[reader] = wr;
    s_pcm_reader_dropped_samples[reader] = 0;
    s_pcm_reader_read_samples[reader] = 0;
    s_pcm_reader_max_level_samples[reader] = 0;
    APP_AUDIO_PCM_MEMORY_BARRIER();
}

uint32_t app_audio_pcm_reader_drop_old_to_frames(app_audio_pcm_reader_t reader,
                                                 uint32_t keep_frames)
{
    uint32_t wr;
    uint32_t rd;
    uint32_t available;
    uint32_t drop_frames;
    uint32_t total_drop_frames = 0U;

    if (!app_audio_pcm_reader_valid(reader)) {
        return 0;
    }

    if (keep_frames > APP_AUDIO_PCM_RING_FRAMES) {
        keep_frames = APP_AUDIO_PCM_RING_FRAMES;
    }

    wr = s_pcm_wr_total;
    APP_AUDIO_PCM_MEMORY_BARRIER();
    rd = s_pcm_rd_total[reader];
    available = wr - rd;

    /* 先计入已经被 producer 覆盖、reader 尚未发现的历史帧。 */
    if (available > APP_AUDIO_PCM_RING_FRAMES) {
        uint32_t overwritten = available - APP_AUDIO_PCM_RING_FRAMES;

        rd += overwritten;
        total_drop_frames += overwritten;
        s_pcm_reader_dropped_samples[reader] += overwritten * APP_AUDIO_PCM_CHANNELS;
        available = APP_AUDIO_PCM_RING_FRAMES;
    }

    if (available <= keep_frames) {
        s_pcm_rd_total[reader] = rd;
        APP_AUDIO_PCM_MEMORY_BARRIER();
        return total_drop_frames;
    }

    drop_frames = available - keep_frames;
    s_pcm_rd_total[reader] = rd + drop_frames;
    s_pcm_reader_dropped_samples[reader] += drop_frames * APP_AUDIO_PCM_CHANNELS;
    APP_AUDIO_PCM_MEMORY_BARRIER();

    return total_drop_frames + drop_frames;
}

uint32_t app_audio_pcm_write_frames(const int16_t *in_lr, uint32_t frames)
{
    uint32_t wr;
    uint32_t i;

    if ((in_lr == NULL) || (frames == 0U)) {
        return 0;
    }

    /* 单次输入大于 ring 时只保留最新一段。当前 I2S callback 不会触发此分支。 */
    if (frames > APP_AUDIO_PCM_RING_FRAMES) {
        uint32_t skip = frames - APP_AUDIO_PCM_RING_FRAMES;

        in_lr += skip * APP_AUDIO_PCM_CHANNELS;
        frames = APP_AUDIO_PCM_RING_FRAMES;
    }

    wr = s_pcm_wr_total;
    app_audio_pcm_copy_into_ring(wr, in_lr, frames);

    /* 数据完全写入后再发布新写位置。 */
    APP_AUDIO_PCM_MEMORY_BARRIER();
    s_pcm_wr_total = wr + frames;
    s_pcm_written_samples += frames * APP_AUDIO_PCM_CHANNELS;

    for (i = 0; i < (uint32_t)APP_AUDIO_PCM_READER_COUNT; i++) {
        uint32_t level_frames = app_audio_pcm_level_frames_snapshot(wr + frames,
                                                                    s_pcm_rd_total[i]);
        uint32_t level_samples = level_frames * APP_AUDIO_PCM_CHANNELS;

        if (level_samples > s_pcm_reader_max_level_samples[i]) {
            s_pcm_reader_max_level_samples[i] = level_samples;
        }
    }

    return frames;
}

uint32_t app_audio_pcm_read_frames_for(app_audio_pcm_reader_t reader,
                                       int16_t *out_lr,
                                       uint32_t max_frames)
{
    uint32_t wr;
    uint32_t rd;
    uint32_t available;
    uint32_t read_frames;

    if (!app_audio_pcm_reader_valid(reader) ||
        (out_lr == NULL) ||
        (max_frames == 0U)) {
        return 0;
    }

    rd = s_pcm_rd_total[reader];
    APP_AUDIO_PCM_MEMORY_BARRIER();
    wr = s_pcm_wr_total;
    available = wr - rd;

    if (available > APP_AUDIO_PCM_RING_FRAMES) {
        uint32_t overwritten = available - APP_AUDIO_PCM_RING_FRAMES;

        rd += overwritten;
        available = APP_AUDIO_PCM_RING_FRAMES;
        s_pcm_reader_dropped_samples[reader] += overwritten * APP_AUDIO_PCM_CHANNELS;
    }

    read_frames = app_audio_pcm_min_u32(max_frames, available);
    if (read_frames == 0U) {
        s_pcm_rd_total[reader] = rd;
        return 0;
    }

    app_audio_pcm_copy_from_ring(rd, out_lr, read_frames);

    APP_AUDIO_PCM_MEMORY_BARRIER();
    s_pcm_rd_total[reader] = rd + read_frames;
    s_pcm_reader_read_samples[reader] += read_frames * APP_AUDIO_PCM_CHANNELS;

    return read_frames;
}

uint32_t app_audio_pcm_read_frames(int16_t *out_lr, uint32_t max_frames)
{
    return app_audio_pcm_read_frames_for(APP_AUDIO_PCM_READER_WIFI,
                                         out_lr,
                                         max_frames);
}

uint32_t app_audio_pcm_write(const int16_t *in, uint32_t samples)
{
    uint32_t frames;

    if ((in == NULL) || (samples < APP_AUDIO_PCM_CHANNELS)) {
        return 0;
    }

    frames = samples / APP_AUDIO_PCM_CHANNELS;
    return app_audio_pcm_write_frames(in, frames) * APP_AUDIO_PCM_CHANNELS;
}

uint32_t app_audio_pcm_read_for(app_audio_pcm_reader_t reader,
                                int16_t *out,
                                uint32_t max_samples)
{
    uint32_t max_frames;

    if ((out == NULL) || (max_samples < APP_AUDIO_PCM_CHANNELS)) {
        return 0;
    }

    max_frames = max_samples / APP_AUDIO_PCM_CHANNELS;
    return app_audio_pcm_read_frames_for(reader, out, max_frames) * APP_AUDIO_PCM_CHANNELS;
}

uint32_t app_audio_pcm_read(int16_t *out, uint32_t max_samples)
{
    return app_audio_pcm_read_for(APP_AUDIO_PCM_READER_WIFI,
                                  out,
                                  max_samples);
}

uint32_t app_audio_pcm_write_from_i2s24_stereo_ex(const uint8_t *buf,
                                                  uint32_t len,
                                                  uint8_t mute_output,
                                                  app_audio_pcm_i2s_stats_t *stats)
{
    uint32_t frames;
    uint32_t i;
    int64_t l_dc_sum = 0;
    int64_t r_dc_sum = 0;
    int32_t l_dc;
    int32_t r_dc;
    uint64_t l_ac_sum = 0;
    uint64_t r_ac_sum = 0;
    uint32_t l_peak = 0;
    uint32_t r_peak = 0;
    uint32_t l_zero = 0;
    uint32_t r_zero = 0;
    uint32_t written_frames;

    if ((buf == NULL) || (len < 8U)) {
        return 0;
    }

    frames = len / 8U;
    if (frames == 0U) {
        return 0;
    }
    if (frames > APP_AUDIO_PCM_MAX_FRAMES_PER_CALL) {
        frames = APP_AUDIO_PCM_MAX_FRAMES_PER_CALL;
    }

    /* 第一遍只计算双声道 DC。 */
    for (i = 0; i < frames; i++) {
        int32_t l = app_audio_pcm_s24_to_s32(&buf[i * 8U + 0U]);
        int32_t r = app_audio_pcm_s24_to_s32(&buf[i * 8U + 4U]);

        l_dc_sum += l;
        r_dc_sum += r;
    }

    l_dc = (int32_t)(l_dc_sum / (int32_t)frames);
    r_dc = (int32_t)(r_dc_sum / (int32_t)frames);

    /* 第二遍同时完成去 DC、PCM16 转换，以及可选幅度统计。 */
    for (i = 0; i < frames; i++) {
        int32_t l = app_audio_pcm_s24_to_s32(&buf[i * 8U + 0U]);
        int32_t r = app_audio_pcm_s24_to_s32(&buf[i * 8U + 4U]);
        int32_t l_ac = l - l_dc;
        int32_t r_ac = r - r_dc;
        uint32_t out_idx = i * APP_AUDIO_PCM_CHANNELS;

        if (stats != NULL) {
            uint32_t al = app_audio_pcm_abs_s32(l_ac);
            uint32_t ar = app_audio_pcm_abs_s32(r_ac);

            if (l == 0) {
                l_zero++;
            }
            if (r == 0) {
                r_zero++;
            }
            if (al > l_peak) {
                l_peak = al;
            }
            if (ar > r_peak) {
                r_peak = ar;
            }
            l_ac_sum += al;
            r_ac_sum += ar;
        }

        if (mute_output) {
            s_pcm_tmp[out_idx + 0U] = 0;
            s_pcm_tmp[out_idx + 1U] = 0;
        } else {
            s_pcm_tmp[out_idx + 0U] = app_audio_pcm_clip_s16(l_ac >> APP_AUDIO_PCM_24_TO_16_SHIFT);
            s_pcm_tmp[out_idx + 1U] = app_audio_pcm_clip_s16(r_ac >> APP_AUDIO_PCM_24_TO_16_SHIFT);
        }
    }

    if (stats != NULL) {
        stats->frames = frames;
        stats->left_peak = l_peak;
        stats->left_avg = (uint32_t)(l_ac_sum / frames);
        stats->left_zero = l_zero;
        stats->left_dc = l_dc;
        stats->right_peak = r_peak;
        stats->right_avg = (uint32_t)(r_ac_sum / frames);
        stats->right_zero = r_zero;
        stats->right_dc = r_dc;
    }

    written_frames = app_audio_pcm_write_frames(s_pcm_tmp, frames);
    return written_frames * APP_AUDIO_PCM_CHANNELS;
}

uint32_t app_audio_pcm_write_from_i2s24_stereo(const uint8_t *buf, uint32_t len)
{
    return app_audio_pcm_write_from_i2s24_stereo_ex(buf, len, 0U, NULL);
}

uint32_t app_audio_pcm_write_silence_from_i2s_len(uint32_t len)
{
    uint32_t frames = len / 8U;
    uint32_t written_frames;

    if (frames == 0U) {
        return 0;
    }
    if (frames > APP_AUDIO_PCM_MAX_FRAMES_PER_CALL) {
        frames = APP_AUDIO_PCM_MAX_FRAMES_PER_CALL;
    }

    memset(s_pcm_tmp, 0, frames * APP_AUDIO_PCM_CHANNELS * sizeof(int16_t));
    written_frames = app_audio_pcm_write_frames(s_pcm_tmp, frames);

    return written_frames * APP_AUDIO_PCM_CHANNELS;
}

void app_audio_pcm_get_status_for(app_audio_pcm_reader_t reader,
                                  app_audio_pcm_status_t *status)
{
    uint32_t level_frames;
    uint32_t free_frames;

    if ((status == NULL) || !app_audio_pcm_reader_valid(reader)) {
        return;
    }

    level_frames = app_audio_pcm_level_frames_for_reader(reader);
    free_frames = app_audio_pcm_free_frames();

    status->level_samples = level_frames * APP_AUDIO_PCM_CHANNELS;
    status->max_level_samples = s_pcm_reader_max_level_samples[reader];
    status->free_samples = free_frames * APP_AUDIO_PCM_CHANNELS;
    status->dropped_samples = s_pcm_reader_dropped_samples[reader];
    status->written_samples = s_pcm_written_samples;
    status->read_samples = s_pcm_reader_read_samples[reader];
}

void app_audio_pcm_get_status(app_audio_pcm_status_t *status)
{
    app_audio_pcm_get_status_for(APP_AUDIO_PCM_READER_WIFI, status);
}
