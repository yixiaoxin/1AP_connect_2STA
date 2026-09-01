#ifndef __APP_AUDIO_PCM_H__
#define __APP_AUDIO_PCM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PCM ring reader id.
 *
 * - WIFI reader：实时 TCP 音频链路使用；旧 app_audio_pcm_read_frames() 默认也走它。
 * - UAC  reader：USB Audio 直通本地 MIC 时使用。
 * - MIXER reader：AP 端混音器读取本地三角麦时使用。
 */
typedef enum {
    APP_AUDIO_PCM_READER_WIFI  = 0,
    APP_AUDIO_PCM_READER_UAC   = 1,
    APP_AUDIO_PCM_READER_MIXER = 2,
    APP_AUDIO_PCM_READER_COUNT
} app_audio_pcm_reader_t;

typedef struct {
    /* 以下计数均按 int16_t sample 统计；stereo 下 1 frame = 2 samples。 */
    uint32_t level_samples;
    uint32_t max_level_samples;
    uint32_t free_samples;
    uint32_t dropped_samples;
    uint32_t written_samples;
    uint32_t read_samples;
} app_audio_pcm_status_t;

/*
 * I2S 24bit stereo -> PCM16 转换时可选返回的幅度统计。
 * 统计与转换共用同一轮扫描，避免 I2S callback 重复遍历 DMA buffer。
 */
typedef struct {
    uint32_t frames;

    uint32_t left_peak;
    uint32_t left_avg;
    uint32_t left_zero;
    int32_t  left_dc;

    uint32_t right_peak;
    uint32_t right_avg;
    uint32_t right_zero;
    int32_t  right_dc;
} app_audio_pcm_i2s_stats_t;

void app_audio_pcm_init(void);
void app_audio_pcm_reset(void);

/* 重置指定 reader 到当前写位置，只读取之后的新音频。 */
void app_audio_pcm_reader_reset(app_audio_pcm_reader_t reader);

/*
 * 将指定 reader 的历史缓存丢到只剩 keep_frames 帧。
 * 返回实际丢弃的 stereo frame 数。
 */
uint32_t app_audio_pcm_reader_drop_old_to_frames(app_audio_pcm_reader_t reader,
                                                 uint32_t keep_frames);

/*
 * stereo frame 级接口，排列为 L0,R0,L1,R1...。
 * 1 frame = 2 个 int16_t sample = 4 bytes。
 */
uint32_t app_audio_pcm_write_frames(const int16_t *in_lr, uint32_t frames);
uint32_t app_audio_pcm_read_frames_for(app_audio_pcm_reader_t reader,
                                       int16_t *out_lr,
                                       uint32_t max_frames);
uint32_t app_audio_pcm_read_frames(int16_t *out_lr, uint32_t max_frames);

/* 兼容旧 sample 级接口。stereo 下 samples 必须为偶数。 */
uint32_t app_audio_pcm_write(const int16_t *in, uint32_t samples);
uint32_t app_audio_pcm_read_for(app_audio_pcm_reader_t reader,
                                int16_t *out,
                                uint32_t max_samples);
uint32_t app_audio_pcm_read(int16_t *out, uint32_t max_samples);

/*
 * I2S 原始数据转 PCM16 stereo 并批量写入 ring。
 * 输入格式：24bit stereo / 32bit slot，8 bytes/frame。
 * mute_output=1 时仍分析真实输入，但向 ring 写入等长静音。
 * stats=NULL 时跳过峰值/平均值等附加统计。
 * 返回写入的 int16_t sample 数。
 */
uint32_t app_audio_pcm_write_from_i2s24_stereo_ex(const uint8_t *buf,
                                                  uint32_t len,
                                                  uint8_t mute_output,
                                                  app_audio_pcm_i2s_stats_t *stats);
uint32_t app_audio_pcm_write_from_i2s24_stereo(const uint8_t *buf, uint32_t len);

/* 根据 I2S len 写入等长 stereo 静音，返回写入 sample 数。 */
uint32_t app_audio_pcm_write_silence_from_i2s_len(uint32_t len);

/* 旧接口默认查询 WIFI reader。 */
uint32_t app_audio_pcm_level_for(app_audio_pcm_reader_t reader);
uint32_t app_audio_pcm_level(void);

void app_audio_pcm_get_status_for(app_audio_pcm_reader_t reader,
                                  app_audio_pcm_status_t *status);
void app_audio_pcm_get_status(app_audio_pcm_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
