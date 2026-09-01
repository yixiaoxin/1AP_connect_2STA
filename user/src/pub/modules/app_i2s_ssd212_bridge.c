#include "modules/app_i2s_ssd212_bridge.h"
#include "modules/app_audio_link.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "dbg.h"
#include "rtos.h"
#include "rtos_al.h"
#include "rtos_def.h"
#include "plf.h"

#include "../../../../audio/asio/asio.h"
#include "../../../../plf/aic8800m40/src/driver/sysctrl/sysctrl_api.h"
#include "../../../../plf/aic8800m40/src/driver/iomux/reg_iomux.h"

/*
 * Full-duplex bridge:
 *
 *   UAC-STA -> Wi-Fi TCP -> AIC8800 I2S0 DOUT -> SSD212
 *   SSD212  -> AIC8800 I2S0 DIN -> Wi-Fi TCP -> UAC-STA
 *
 * I2S clock role is selected by APP_SSD212_I2S_AIC_MASTER_ENABLE.
 * The default is AIC8800 slave and SSD212 master, matching the original
 * digital-microphone interface direction.
 * Wire format:
 *   LRCK 48 kHz, BCK 3.072 MHz, stereo, 16 valid bits in each 32-bit slot.
 *
 * The current AIC ASIO driver creates a 32-bit slot when AUD_BITS_24 is used:
 *   24 data bits + 8 BCK extension bits = 32 BCK per channel.
 * We therefore carry PCM16 as the most-significant 16 bits of the 24-bit
 * audio word.  The final 8 bits of the 24-bit word and the 8 extension clocks
 * are zero.
 */

#if (APP_SSD212_I2S_AIC_MASTER_ENABLE > 1U)
#error "APP_SSD212_I2S_AIC_MASTER_ENABLE must be 0 or 1."
#endif

#if APP_SSD212_I2S_AIC_MASTER_ENABLE
/* AIC master mode requires the accurate 12.288 MHz HCLK-derived source. */
#if !defined(PLF_HCLK_MCLK) || (PLF_HCLK_MCLK == 0)
#error "AIC8800 I2S master mode requires build option HCLK_MCLK=on"
#endif
#define APP_SSD212_I2S_DEVICE_ROLE AUD_DEVICE_ROLE_MASTER
#else
#define APP_SSD212_I2S_DEVICE_ROLE AUD_DEVICE_ROLE_SLAVE
#endif

#define APP_SSD212_I2S_IOMUX_SEL              6U
#define APP_SSD212_I2S_STREAM_GROUP            AUD_STREAM_GROUP_0
#define APP_SSD212_I2S_SAMPLE_RATE             AUD_SAMPRATE_48000
#define APP_SSD212_I2S_SAMPLE_RATE_HZ          48000U
#define APP_SSD212_I2S_MCLK_HZ                 12288000U
#define APP_SSD212_I2S_BCK_PER_FRAME           64U
#define APP_SSD212_I2S_CHANNELS                2U
#define APP_SSD212_I2S_SLOT_BYTES              4U
#define APP_SSD212_I2S_FRAME_BYTES             (APP_SSD212_I2S_CHANNELS * APP_SSD212_I2S_SLOT_BYTES)

/* Match the existing UAC wire packet period: 5 ms / 240 frames / 960 bytes. */
#define APP_SSD212_I2S_BLOCK_MS                APP_AUDIO_LINK_UAC_UPLINK_PACKET_MS
#define APP_SSD212_I2S_BLOCK_FRAMES            APP_AUDIO_LINK_UAC_UPLINK_FRAMES_PER_PKT
#define APP_SSD212_I2S_BLOCK_SAMPLES           (APP_SSD212_I2S_BLOCK_FRAMES * APP_SSD212_I2S_CHANNELS)
#define APP_SSD212_I2S_BLOCK_PCM_BYTES         (APP_SSD212_I2S_BLOCK_SAMPLES * sizeof(int16_t))
#define APP_SSD212_I2S_BLOCK_RAW_BYTES         (APP_SSD212_I2S_BLOCK_FRAMES * APP_SSD212_I2S_FRAME_BYTES)

/* ASIO DMA is ping-pong: total buffer is two 5 ms halves. */
#define APP_SSD212_I2S_DMA_BYTES               (APP_SSD212_I2S_BLOCK_RAW_BYTES * 2U)

/* Two 5 ms blocks form the existing link-layer 10 ms internal block. */
#define APP_SSD212_I2S_LINK_FRAMES             APP_AUDIO_LINK_UAC_FRAMES_PER_PKT
#define APP_SSD212_I2S_LINK_SAMPLES            (APP_SSD212_I2S_LINK_FRAMES * APP_SSD212_I2S_CHANNELS)

/* Keep capture and playback queue sizes independent.  Capture is the
 * time-critical SSD212->AP path: eight 5 ms slots provide 40 ms of scheduler
 * headroom without adding latency to the normal path.  Playback keeps the
 * original four slots because it is already stable. */
#ifndef APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH
#define APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH     8U
#endif

#ifndef APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH
#define APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH    4U
#endif

#ifndef APP_SSD212_I2S_LOG_INTERVAL_MS
/* Periodic multi-line UART output used to run in the same worker that drains
 * the 5 ms capture queue.  Disable it in the realtime build; startup/fault
 * logs remain available. */
#define APP_SSD212_I2S_LOG_INTERVAL_MS         0U
#endif

#ifndef APP_SSD212_I2S_WORKER_STACK
#define APP_SSD212_I2S_WORKER_STACK            2048U
#endif

#ifndef APP_SSD212_I2S_WORKER_PRIO
#define APP_SSD212_I2S_WORKER_PRIO             6U
#endif

/* Number of leading PCM samples/raw I2S words retained for periodic diagnosis. */
#define APP_SSD212_I2S_DIAG_SAMPLE_COUNT        4U

#define APP_SSD212_I2S_TEST_SINE_HZ             1000U
#define APP_SSD212_I2S_TEST_SINE_SAMPLES        (APP_SSD212_I2S_SAMPLE_RATE_HZ / APP_SSD212_I2S_TEST_SINE_HZ)

#if (APP_SSD212_I2S_TEST_SINE_ENABLE > 1U)
#error "APP_SSD212_I2S_TEST_SINE_ENABLE must be 0 or 1."
#endif

#if (APP_SSD212_I2S_TEST_SINE_AMPLITUDE > 32767U)
#error "APP_SSD212_I2S_TEST_SINE_AMPLITUDE must be in the PCM16 range 0..32767."
#endif

#if ((APP_SSD212_I2S_SAMPLE_RATE_HZ % APP_SSD212_I2S_TEST_SINE_HZ) != 0U)
#error "The sine-test frequency must divide the I2S sample rate exactly."
#endif

#if (APP_SSD212_I2S_TEST_SINE_SAMPLES != 48U)
#error "The 1 kHz/48 kHz sine table must contain exactly 48 samples."
#endif

#if (APP_SSD212_I2S_BLOCK_MS != 5U)
#error "SSD212 bridge expects the existing UAC wire period to remain 5 ms."
#endif

#if (APP_SSD212_I2S_BLOCK_FRAMES != 240U)
#error "SSD212 bridge expects 240 stereo frames per 5 ms block at 48 kHz."
#endif

#if (APP_SSD212_I2S_LINK_FRAMES != (APP_SSD212_I2S_BLOCK_FRAMES * 2U))
#error "The existing UAC internal block must remain two 5 ms packets."
#endif

static uint8_t s_capture_dma[APP_SSD212_I2S_DMA_BYTES] __attribute__((aligned(4)));
static uint8_t s_playback_dma[APP_SSD212_I2S_DMA_BYTES] __attribute__((aligned(4)));

static int16_t s_capture_callback_pcm[APP_SSD212_I2S_BLOCK_SAMPLES] __attribute__((aligned(4)));
static int16_t s_playback_callback_pcm[APP_SSD212_I2S_BLOCK_SAMPLES] __attribute__((aligned(4)));

static int16_t s_capture_queue[APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH][APP_SSD212_I2S_BLOCK_SAMPLES]
    __attribute__((aligned(4)));
static int16_t s_playback_queue[APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH][APP_SSD212_I2S_BLOCK_SAMPLES]
    __attribute__((aligned(4)));

static volatile uint8_t s_capture_q_rd;
static volatile uint8_t s_capture_q_wr;
static volatile uint8_t s_capture_q_count;
static volatile uint8_t s_playback_q_rd;
static volatile uint8_t s_playback_q_wr;
static volatile uint8_t s_playback_q_count;

static int16_t s_ssd212_to_uac_10ms[APP_SSD212_I2S_LINK_SAMPLES] __attribute__((aligned(4)));
static int16_t s_uac_to_ssd212_10ms[APP_SSD212_I2S_LINK_SAMPLES] __attribute__((aligned(4)));
static uint8_t s_ssd212_to_uac_half_count;

static volatile uint8_t s_started;
static volatile uint8_t s_capture_opened;
static volatile uint8_t s_playback_opened;
static volatile uint8_t s_worker_running;
static volatile uint8_t s_worker_stop;
static volatile uint8_t s_asio_inited;
static rtos_task_handle s_worker_task;

static app_i2s_ssd212_bridge_stats_t s_stats;

#if APP_SSD212_I2S_TEST_SINE_ENABLE
/* One exact 1 kHz cycle at 48 kHz, normalized to signed Q15. */
static const int16_t s_test_sine_q15[APP_SSD212_I2S_TEST_SINE_SAMPLES] = {
         0,   4277,   8481,  12539,  16383,  19947,  23170,  25996,
     28377,  30273,  31650,  32487,  32767,  32487,  31650,  30273,
     28377,  25996,  23170,  19947,  16383,  12539,   8481,   4277,
         0,  -4277,  -8481, -12539, -16383, -19947, -23170, -25996,
    -28377, -30273, -31650, -32487, -32767, -32487, -31650, -30273,
    -28377, -25996, -23170, -19947, -16384, -12539,  -8481,  -4277
};
static int16_t s_test_sine_pcm[APP_SSD212_I2S_TEST_SINE_SAMPLES];
static uint32_t s_test_sine_phase;
#endif

/*
 * DMA callbacks only update these snapshots. Printing is done by the worker
 * task, never from the callback/interrupt context.
 */
static volatile int16_t s_diag_uac_rx_first[APP_SSD212_I2S_DIAG_SAMPLE_COUNT];
static volatile int16_t s_diag_playback_first[APP_SSD212_I2S_DIAG_SAMPLE_COUNT];
static volatile uint32_t s_diag_capture_raw_first[APP_SSD212_I2S_DIAG_SAMPLE_COUNT];
static volatile uint32_t s_diag_playback_raw_first[APP_SSD212_I2S_DIAG_SAMPLE_COUNT];
static uint8_t s_diag_reported_uac_nonzero;
static uint8_t s_diag_reported_tx_nonzero;
static uint8_t s_diag_reported_rx_nonzero;

static void app_i2s_ssd212_iomux_one(uint8_t pin)
{
    iomux_gpioa_config_sel_setf(pin, APP_SSD212_I2S_IOMUX_SEL);
    iomux_gpioa_config_pull_up_setf(pin, 0U);
    iomux_gpioa_config_pull_dn_setf(pin, 0U);
    iomux_gpioa_config_pull_frc_setf(pin, 0U);
}

static void app_i2s_ssd212_iomux_config(void)
{
#if APP_SSD212_I2S_AIC_MASTER_ENABLE
    /* A0=LRCK output, A1=BCK output, A2=DIN input, A3=DOUT output. */
#else
    /* A0=LRCK input, A1=BCK input, A2=DIN input, A3=DOUT output. */
#endif
    app_i2s_ssd212_iomux_one(0U);
    app_i2s_ssd212_iomux_one(1U);
    app_i2s_ssd212_iomux_one(2U);
    app_i2s_ssd212_iomux_one(3U);
}

static void app_i2s_ssd212_iomux_diag(const char *stage)
{
    dbg("I2S_PIN[%s]: A0(LRCK)=%u A1(BCK)=%u A2(DIN)=%u A3(DOUT)=%u expected_mux=%u\r\n",
        stage ? stage : "?",
        (unsigned int)iomux_gpioa_config_sel_getf(0U),
        (unsigned int)iomux_gpioa_config_sel_getf(1U),
        (unsigned int)iomux_gpioa_config_sel_getf(2U),
        (unsigned int)iomux_gpioa_config_sel_getf(3U),
        (unsigned int)APP_SSD212_I2S_IOMUX_SEL);
}

static void app_i2s_ssd212_clock_diag(const char *stage)
{
#if APP_SSD212_I2S_AIC_MASTER_ENABLE
    uint32_t hclk = sysctrl_clock_get(SYS_HCLK);
    uint32_t pcm_reg = AIC_AONSYSCTRL->pcm_clk_div;
    uint32_t codec_reg = AIC_CPUSYSCTRL->codec_mclk_div;
    uint32_t bclk_sel = AIC_CPUSYSCTRL->bclk_sel;
    uint32_t denom = pcm_reg & 0x0FFFU;
    uint32_t numer = (pcm_reg >> 12) & 0x0FFFU;
    uint32_t mclk = 0U;
    uint32_t bck = 0U;
    uint32_t lrck = 0U;
    uint32_t mclk_div = codec_reg & 0x3FU;

    if ((hclk != 0U) && (denom != 0U)) {
        mclk = (uint32_t)(((uint64_t)hclk * numer) / denom);
    }
    if ((mclk != 0U) && (mclk_div != 0U)) {
        bck = mclk / mclk_div;
        lrck = bck / APP_SSD212_I2S_BCK_PER_FRAME;
    }

    dbg("I2S_CLK[%s]: role=MASTER hclk=%u pcm_div=0x%08x(num=%u den=%u) mclk=%u codec_div=0x%08x(div=%u) bclk_sel=0x%08x calc_bck=%u calc_lrck=%u\r\n",
        stage ? stage : "?",
        (unsigned int)hclk,
        (unsigned int)pcm_reg,
        (unsigned int)numer,
        (unsigned int)denom,
        (unsigned int)mclk,
        (unsigned int)codec_reg,
        (unsigned int)mclk_div,
        (unsigned int)bclk_sel,
        (unsigned int)bck,
        (unsigned int)lrck);
#else
    dbg("I2S_CLK[%s]: role=SLAVE; LRCK/BCK are external inputs from SSD212, expected LRCK=48000 BCK=3072000; use fs_est to verify the actual external rate\r\n",
        stage ? stage : "?");
#endif
}

static uint32_t app_i2s_ssd212_raw_word_get(const uint8_t *src)
{
    uint32_t raw = 0U;

    memcpy(&raw, src, sizeof(raw));
    return raw;
}

static int16_t app_i2s_ssd212_unpack_raw_pcm16(uint32_t raw)
{
    int32_t sample24 = (int32_t)(raw & 0x00FFFFFFU);

    if ((sample24 & 0x00800000L) != 0) {
        sample24 |= (int32_t)0xFF000000L;
    }

    return (int16_t)(sample24 >> 8);
}

static void app_i2s_ssd212_pack_pcm16(uint8_t *dst, int16_t sample)
{
    int32_t sample24 = ((int32_t)sample) << 8;
    uint32_t raw = ((uint32_t)sample24) & 0x00FFFFFFU;

    memcpy(dst, &raw, sizeof(raw));
}

static uint32_t app_i2s_ssd212_abs_s16(int16_t sample)
{
    int32_t value = sample;

    if (value < 0) {
        value = -value;
    }
    return (uint32_t)value;
}

static void app_i2s_ssd212_analyze_pcm16(const int16_t *pcm,
                                          uint32_t frames,
                                          uint32_t *left_peak,
                                          uint32_t *right_peak)
{
    uint32_t frame;
    uint32_t lpk = 0U;
    uint32_t rpk = 0U;

    if (pcm != NULL) {
        for (frame = 0U; frame < frames; frame++) {
            uint32_t left_abs = app_i2s_ssd212_abs_s16(pcm[frame * 2U]);
            uint32_t right_abs = app_i2s_ssd212_abs_s16(pcm[frame * 2U + 1U]);

            if (left_abs > lpk) {
                lpk = left_abs;
            }
            if (right_abs > rpk) {
                rpk = right_abs;
            }
        }
    }

    if (left_peak != NULL) {
        *left_peak = lpk;
    }
    if (right_peak != NULL) {
        *right_peak = rpk;
    }
}

static void app_i2s_ssd212_queue_reset(void)
{
    uint32_t protect = rtos_protect();

    s_capture_q_rd = 0U;
    s_capture_q_wr = 0U;
    s_capture_q_count = 0U;
    s_playback_q_rd = 0U;
    s_playback_q_wr = 0U;
    s_playback_q_count = 0U;

    rtos_unprotect(protect);
}

/* Capture callback is producer, worker is consumer.  Keep the newest audio. */
static void app_i2s_ssd212_capture_queue_push(const int16_t *pcm)
{
    uint32_t protect = rtos_protect();
    uint8_t wr;

    if (s_capture_q_count >= APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH) {
        s_capture_q_rd = (uint8_t)((s_capture_q_rd + 1U) % APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH);
        s_capture_q_count--;
        s_stats.capture_queue_overflow++;
    }

    wr = s_capture_q_wr;
    memcpy(s_capture_queue[wr], pcm, APP_SSD212_I2S_BLOCK_PCM_BYTES);
    s_capture_q_wr = (uint8_t)((wr + 1U) % APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH);
    s_capture_q_count++;

    rtos_unprotect(protect);
}

static int app_i2s_ssd212_capture_queue_pop(int16_t *pcm)
{
    uint32_t protect = rtos_protect();
    uint8_t rd;

    if (s_capture_q_count == 0U) {
        rtos_unprotect(protect);
        return 0;
    }

    rd = s_capture_q_rd;
    memcpy(pcm, s_capture_queue[rd], APP_SSD212_I2S_BLOCK_PCM_BYTES);
    s_capture_q_rd = (uint8_t)((rd + 1U) % APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH);
    s_capture_q_count--;

    rtos_unprotect(protect);
    return 1;
}

static uint8_t app_i2s_ssd212_capture_queue_level(void)
{
    uint32_t protect = rtos_protect();
    uint8_t count = s_capture_q_count;
    rtos_unprotect(protect);
    return count;
}

/* Worker is producer, playback callback is consumer. */
static int app_i2s_ssd212_playback_queue_push(const int16_t *pcm)
{
    uint32_t protect = rtos_protect();
    uint8_t wr;

    if (s_playback_q_count >= APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH) {
        s_stats.playback_queue_overflow++;
        rtos_unprotect(protect);
        return 0;
    }

    wr = s_playback_q_wr;
    memcpy(s_playback_queue[wr], pcm, APP_SSD212_I2S_BLOCK_PCM_BYTES);
    s_playback_q_wr = (uint8_t)((wr + 1U) % APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH);
    s_playback_q_count++;

    rtos_unprotect(protect);
    return 1;
}

static int app_i2s_ssd212_playback_queue_pop(int16_t *pcm)
{
    uint32_t protect = rtos_protect();
    uint8_t rd;

    if (s_playback_q_count == 0U) {
        rtos_unprotect(protect);
        return 0;
    }

    rd = s_playback_q_rd;
    memcpy(pcm, s_playback_queue[rd], APP_SSD212_I2S_BLOCK_PCM_BYTES);
    s_playback_q_rd = (uint8_t)((rd + 1U) % APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH);
    s_playback_q_count--;

    rtos_unprotect(protect);
    return 1;
}

static uint8_t app_i2s_ssd212_playback_queue_level(void)
{
    uint32_t protect = rtos_protect();
    uint8_t count = s_playback_q_count;
    rtos_unprotect(protect);
    return count;
}

static uint32_t app_i2s_ssd212_capture_callback(uint8_t *buf, uint32_t len)
{
    uint32_t frame;
    uint32_t left_peak = 0U;
    uint32_t right_peak = 0U;
    uint32_t raw_nonzero_words = 0U;

    s_stats.capture_callbacks++;

    if ((buf == NULL) || (len != APP_SSD212_I2S_BLOCK_RAW_BYTES)) {
        s_stats.bad_capture_len++;
        return len;
    }

    for (frame = 0U; frame < APP_SSD212_I2S_BLOCK_FRAMES; frame++) {
        const uint8_t *src = buf + (frame * APP_SSD212_I2S_FRAME_BYTES);
        uint32_t left_raw = app_i2s_ssd212_raw_word_get(src);
        uint32_t right_raw = app_i2s_ssd212_raw_word_get(src + APP_SSD212_I2S_SLOT_BYTES);
        int16_t left = app_i2s_ssd212_unpack_raw_pcm16(left_raw);
        int16_t right = app_i2s_ssd212_unpack_raw_pcm16(right_raw);
        uint32_t left_abs = app_i2s_ssd212_abs_s16(left);
        uint32_t right_abs = app_i2s_ssd212_abs_s16(right);

        if (left_raw != 0U) {
            raw_nonzero_words++;
        }
        if (right_raw != 0U) {
            raw_nonzero_words++;
        }

        if (frame < 2U) {
            s_diag_capture_raw_first[frame * 2U] = left_raw;
            s_diag_capture_raw_first[frame * 2U + 1U] = right_raw;
        }

        s_capture_callback_pcm[frame * 2U] = left;
        s_capture_callback_pcm[frame * 2U + 1U] = right;

        if (left_abs > left_peak) {
            left_peak = left_abs;
        }
        if (right_abs > right_peak) {
            right_peak = right_abs;
        }
    }

    s_stats.capture_frames += APP_SSD212_I2S_BLOCK_FRAMES;
    s_stats.capture_left_peak = left_peak;
    s_stats.capture_right_peak = right_peak;
    s_stats.capture_raw_nonzero_words += raw_nonzero_words;
    app_i2s_ssd212_capture_queue_push(s_capture_callback_pcm);
    return len;
}

#if APP_SSD212_I2S_TEST_SINE_ENABLE
static void app_i2s_ssd212_prepare_test_sine(void)
{
    uint32_t sample;

    /* Scale the Q15 table once during startup. Keep division and multiply
     * operations out of the 5 ms DMA callback. */
    for (sample = 0U; sample < APP_SSD212_I2S_TEST_SINE_SAMPLES; sample++) {
        s_test_sine_pcm[sample] = (int16_t)(
            ((int32_t)s_test_sine_q15[sample] *
             (int32_t)APP_SSD212_I2S_TEST_SINE_AMPLITUDE) / 32767L);
    }
    s_test_sine_phase = 0U;
}

static void app_i2s_ssd212_generate_test_sine(int16_t *pcm, uint32_t frames)
{
    uint32_t frame;

    for (frame = 0U; frame < frames; frame++) {
        int16_t sample = s_test_sine_pcm[s_test_sine_phase];

        /* Send the same 1 kHz tone to left and right channels. */
        pcm[frame * 2U] = sample;
        pcm[frame * 2U + 1U] = sample;

        s_test_sine_phase++;
        if (s_test_sine_phase >= APP_SSD212_I2S_TEST_SINE_SAMPLES) {
            s_test_sine_phase = 0U;
        }
    }
}
#endif

static uint32_t app_i2s_ssd212_playback_callback(uint8_t *buf, uint32_t len)
{
    uint32_t frame;
    uint32_t left_peak = 0U;
    uint32_t right_peak = 0U;
    uint32_t raw_nonzero_words = 0U;

    s_stats.playback_callbacks++;

    if ((buf == NULL) || (len != APP_SSD212_I2S_BLOCK_RAW_BYTES)) {
        if ((buf != NULL) && (len > 0U)) {
            memset(buf, 0, len);
        }
        s_stats.bad_playback_len++;
        return len;
    }

#if APP_SSD212_I2S_TEST_SINE_ENABLE
    /* Test mode bypasses Wi-Fi/UAC and feeds a deterministic stereo sine
     * directly into the same PCM16 -> 24-bit/32-slot packing path. */
    app_i2s_ssd212_generate_test_sine(s_playback_callback_pcm,
                                      APP_SSD212_I2S_BLOCK_FRAMES);
#else
    if (!app_i2s_ssd212_playback_queue_pop(s_playback_callback_pcm)) {
        memset(s_playback_callback_pcm, 0, sizeof(s_playback_callback_pcm));
        s_stats.uac_to_ssd212_underflow++;
    }
#endif

    app_i2s_ssd212_analyze_pcm16(s_playback_callback_pcm,
                                  APP_SSD212_I2S_BLOCK_FRAMES,
                                  &left_peak,
                                  &right_peak);
    s_stats.playback_left_peak = left_peak;
    s_stats.playback_right_peak = right_peak;
    if ((left_peak == 0U) && (right_peak == 0U)) {
        s_stats.playback_zero_callbacks++;
    } else {
        s_stats.playback_nonzero_callbacks++;
    }

    for (frame = 0U; frame < APP_SSD212_I2S_DIAG_SAMPLE_COUNT; frame++) {
        s_diag_playback_first[frame] = s_playback_callback_pcm[frame];
    }

    for (frame = 0U; frame < APP_SSD212_I2S_BLOCK_FRAMES; frame++) {
        uint8_t *dst = buf + (frame * APP_SSD212_I2S_FRAME_BYTES);
        uint32_t left_raw;
        uint32_t right_raw;

        app_i2s_ssd212_pack_pcm16(dst,
                                  s_playback_callback_pcm[frame * 2U]);
        app_i2s_ssd212_pack_pcm16(dst + APP_SSD212_I2S_SLOT_BYTES,
                                  s_playback_callback_pcm[frame * 2U + 1U]);

        left_raw = app_i2s_ssd212_raw_word_get(dst);
        right_raw = app_i2s_ssd212_raw_word_get(dst + APP_SSD212_I2S_SLOT_BYTES);
        if (left_raw != 0U) {
            raw_nonzero_words++;
        }
        if (right_raw != 0U) {
            raw_nonzero_words++;
        }
        if (frame < 2U) {
            s_diag_playback_raw_first[frame * 2U] = left_raw;
            s_diag_playback_raw_first[frame * 2U + 1U] = right_raw;
        }
    }

    s_stats.playback_raw_nonzero_words += raw_nonzero_words;
    s_stats.playback_frames += APP_SSD212_I2S_BLOCK_FRAMES;
    return len;
}

static void app_i2s_ssd212_worker(void *param)
{
    int16_t rx_5ms[APP_SSD212_I2S_BLOCK_SAMPLES];
#if (APP_SSD212_I2S_LOG_INTERVAL_MS > 0U)
    uint32_t last_log_ms = 0U;
    uint32_t last_capture_callbacks = 0U;
#endif

    (void)param;
    s_worker_running = 1U;

    while (!s_worker_stop) {
        /* SSD212 -> Receiver AP: combine two native 5 ms I2S callbacks into the
         * existing 10 ms link-layer block; link layer still transmits two
         * 5 ms TCP packets exactly as before. */
        while (app_i2s_ssd212_capture_queue_pop(rx_5ms)) {
            memcpy(s_ssd212_to_uac_10ms +
                       ((uint32_t)s_ssd212_to_uac_half_count * APP_SSD212_I2S_BLOCK_SAMPLES),
                   rx_5ms,
                   APP_SSD212_I2S_BLOCK_PCM_BYTES);
            s_ssd212_to_uac_half_count++;

            if (s_ssd212_to_uac_half_count >= 2U) {
                int ret = app_audio_link_ap_send_uac_pcm(s_ssd212_to_uac_10ms,
                                                         APP_SSD212_I2S_LINK_FRAMES);
                if (ret == 0) {
                    s_stats.ssd212_to_uac_blocks++;
                } else {
                    s_stats.ssd212_to_uac_drop++;
                }
                s_ssd212_to_uac_half_count = 0U;
            }
        }

#if !APP_SSD212_I2S_TEST_SINE_ENABLE
        /* Receiver AP -> SSD212: the existing link layer reconstructs one 10 ms
         * block from two 5 ms TCP packets.  Split it back into two 5 ms I2S
         * blocks and keep only a short queue to limit latency. */
        if (app_i2s_ssd212_playback_queue_level() <= 1U) {
            if (app_audio_link_ap_read_uac_rx_pcm(s_uac_to_ssd212_10ms,
                                                   APP_SSD212_I2S_LINK_FRAMES) == 0) {
                uint32_t left_peak = 0U;
                uint32_t right_peak = 0U;
                uint32_t sample;

                app_i2s_ssd212_analyze_pcm16(s_uac_to_ssd212_10ms,
                                              APP_SSD212_I2S_LINK_FRAMES,
                                              &left_peak,
                                              &right_peak);
                s_stats.uac_rx_left_peak = left_peak;
                s_stats.uac_rx_right_peak = right_peak;
                if ((left_peak == 0U) && (right_peak == 0U)) {
                    s_stats.uac_rx_zero_blocks++;
                } else {
                    s_stats.uac_rx_nonzero_blocks++;
                }
                for (sample = 0U; sample < APP_SSD212_I2S_DIAG_SAMPLE_COUNT; sample++) {
                    s_diag_uac_rx_first[sample] = s_uac_to_ssd212_10ms[sample];
                }

                (void)app_i2s_ssd212_playback_queue_push(s_uac_to_ssd212_10ms);
                (void)app_i2s_ssd212_playback_queue_push(
                    s_uac_to_ssd212_10ms + APP_SSD212_I2S_BLOCK_SAMPLES);
                s_stats.uac_to_ssd212_blocks++;
            }
        }
#endif

#if (APP_SSD212_I2S_LOG_INTERVAL_MS > 0U)
        {
            uint32_t now_ms = rtos_now(false);
            if ((last_log_ms == 0U) ||
                ((now_ms - last_log_ms) >= APP_SSD212_I2S_LOG_INTERVAL_MS)) {
                uint32_t elapsed_ms = (last_log_ms == 0U) ? 0U : (now_ms - last_log_ms);
                uint32_t cap_delta = s_stats.capture_callbacks - last_capture_callbacks;
                uint32_t fs_est = 0U;

                if (elapsed_ms != 0U) {
                    fs_est = (uint32_t)(((uint64_t)cap_delta *
                                         APP_SSD212_I2S_BLOCK_FRAMES * 1000U) /
                                        elapsed_ms);
                }
                last_log_ms = now_ms;
                last_capture_callbacks = s_stats.capture_callbacks;
                dbg("I2S_BRIDGE: cap_cb=%u play_cb=%u fs_est=%u s2u=%u drop=%u u2s=%u under=%u q=%u/%u qov=%u/%u bad=%u/%u uac=%u\r\n",
                    (unsigned int)s_stats.capture_callbacks,
                    (unsigned int)s_stats.playback_callbacks,
                    (unsigned int)fs_est,
                    (unsigned int)s_stats.ssd212_to_uac_blocks,
                    (unsigned int)s_stats.ssd212_to_uac_drop,
                    (unsigned int)s_stats.uac_to_ssd212_blocks,
                    (unsigned int)s_stats.uac_to_ssd212_underflow,
                    (unsigned int)app_i2s_ssd212_capture_queue_level(),
                    (unsigned int)app_i2s_ssd212_playback_queue_level(),
                    (unsigned int)s_stats.capture_queue_overflow,
                    (unsigned int)s_stats.playback_queue_overflow,
                    (unsigned int)s_stats.bad_capture_len,
                    (unsigned int)s_stats.bad_playback_len,
                    (unsigned int)app_audio_link_ap_is_uac_connected());

                dbg("I2S_DIAG WIFI_RX(Receiver->STA): peak=%u/%u nz_blk=%u zero_blk=%u first=%d,%d,%d,%d\r\n",
                    (unsigned int)s_stats.uac_rx_left_peak,
                    (unsigned int)s_stats.uac_rx_right_peak,
                    (unsigned int)s_stats.uac_rx_nonzero_blocks,
                    (unsigned int)s_stats.uac_rx_zero_blocks,
                    (int)s_diag_uac_rx_first[0],
                    (int)s_diag_uac_rx_first[1],
                    (int)s_diag_uac_rx_first[2],
                    (int)s_diag_uac_rx_first[3]);

                dbg("I2S_DIAG I2S_TX(STA->SSD): peak=%u/%u nz_cb=%u zero_cb=%u raw_nz=%u pcm=%d,%d,%d,%d raw=%08x,%08x,%08x,%08x\r\n",
                    (unsigned int)s_stats.playback_left_peak,
                    (unsigned int)s_stats.playback_right_peak,
                    (unsigned int)s_stats.playback_nonzero_callbacks,
                    (unsigned int)s_stats.playback_zero_callbacks,
                    (unsigned int)s_stats.playback_raw_nonzero_words,
                    (int)s_diag_playback_first[0],
                    (int)s_diag_playback_first[1],
                    (int)s_diag_playback_first[2],
                    (int)s_diag_playback_first[3],
                    (unsigned int)s_diag_playback_raw_first[0],
                    (unsigned int)s_diag_playback_raw_first[1],
                    (unsigned int)s_diag_playback_raw_first[2],
                    (unsigned int)s_diag_playback_raw_first[3]);

                dbg("I2S_DIAG I2S_RX(SSD->AP): peak=%u/%u raw_nz=%u raw=%08x,%08x,%08x,%08x\r\n",
                    (unsigned int)s_stats.capture_left_peak,
                    (unsigned int)s_stats.capture_right_peak,
                    (unsigned int)s_stats.capture_raw_nonzero_words,
                    (unsigned int)s_diag_capture_raw_first[0],
                    (unsigned int)s_diag_capture_raw_first[1],
                    (unsigned int)s_diag_capture_raw_first[2],
                    (unsigned int)s_diag_capture_raw_first[3]);

#if APP_SSD212_I2S_TEST_SINE_ENABLE
                if (s_stats.playback_nonzero_callbacks > 0U) {
                    dbg("I2S_HINT: 1kHz sine is reaching I2S TX DMA -> if silent, check GPIOA3, SSD212 I2S slave format, RX-to-DAC and PA path.\r\n");
                } else {
                    dbg("I2S_HINT: sine test enabled but I2S TX DMA is still zero.\r\n");
                }
#else
                if ((s_stats.uac_to_ssd212_blocks > 0U) &&
                    (s_stats.uac_rx_nonzero_blocks == 0U)) {
                    dbg("I2S_HINT: Wi-Fi audio blocks arrive, but Receiver PCM is all zero -> check PC/Receiver AP playback source or mute.\r\n");
                } else if ((s_stats.uac_rx_nonzero_blocks > 0U) &&
                           (s_stats.playback_nonzero_callbacks == 0U)) {
                    dbg("I2S_HINT: Receiver PCM is nonzero, but I2S TX DMA remains zero -> check bridge queue/packing.\r\n");
                } else if (s_stats.playback_nonzero_callbacks > 0U) {
                    dbg("I2S_HINT: nonzero PCM has reached I2S TX DMA -> if speaker is silent, probe GPIOA3 and check SSD212 slave format/RX-to-DAC/PA path.\r\n");
                } else {
                    dbg("I2S_HINT: waiting for nonzero UAC playback PCM.\r\n");
                }
#endif

                if ((s_stats.uac_rx_nonzero_blocks > 0U) &&
                    !s_diag_reported_uac_nonzero) {
                    dbg("I2S_EVENT: first nonzero Receiver PCM reached Triangle STA bridge.\r\n");
                    s_diag_reported_uac_nonzero = 1U;
                }
                if ((s_stats.playback_nonzero_callbacks > 0U) &&
                    !s_diag_reported_tx_nonzero) {
                    dbg("I2S_EVENT: first nonzero PCM reached AIC8800 I2S TX DMA.\r\n");
                    s_diag_reported_tx_nonzero = 1U;
                }
                if ((s_stats.capture_raw_nonzero_words > 0U) &&
                    !s_diag_reported_rx_nonzero) {
                    dbg("I2S_EVENT: first nonzero raw word received on AIC8800 I2S DIN.\r\n");
                    s_diag_reported_rx_nonzero = 1U;
                }
            }
        }
#endif

        rtos_task_suspend(1U);
    }

    s_worker_running = 0U;
    s_worker_task = NULL;
    rtos_task_delete(NULL);
}

static void app_i2s_ssd212_cleanup_after_start_fail(void)
{
    s_worker_stop = 1U;

    if (s_capture_opened) {
        asio_stream_stop(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_CAPTURE);
        asio_stream_close(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_CAPTURE);
        s_capture_opened = 0U;
    }

    if (s_playback_opened) {
        asio_stream_stop(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_PLAYBACK);
        asio_stream_close(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_PLAYBACK);
        s_playback_opened = 0U;
    }

    /* Match the proven original I2S module: do not call asio_close(). */
    s_asio_inited = 0U;
    s_started = 0U;
}

int app_i2s_ssd212_bridge_start(void)
{
    ASIO_STREAM_CFG_T capture_cfg;
    ASIO_STREAM_CFG_T playback_cfg;
    int ret;

    if (s_started) {
        return 0;
    }

    memset(&s_stats, 0, sizeof(s_stats));
    memset((void *)s_diag_uac_rx_first, 0, sizeof(s_diag_uac_rx_first));
    memset((void *)s_diag_playback_first, 0, sizeof(s_diag_playback_first));
    memset((void *)s_diag_capture_raw_first, 0, sizeof(s_diag_capture_raw_first));
    memset((void *)s_diag_playback_raw_first, 0, sizeof(s_diag_playback_raw_first));
    s_diag_reported_uac_nonzero = 0U;
    s_diag_reported_tx_nonzero = 0U;
    s_diag_reported_rx_nonzero = 0U;
#if APP_SSD212_I2S_TEST_SINE_ENABLE
    app_i2s_ssd212_prepare_test_sine();
#endif
    memset(s_capture_dma, 0, sizeof(s_capture_dma));
    memset(s_playback_dma, 0, sizeof(s_playback_dma));
    memset(s_ssd212_to_uac_10ms, 0, sizeof(s_ssd212_to_uac_10ms));
    memset(s_uac_to_ssd212_10ms, 0, sizeof(s_uac_to_ssd212_10ms));
    s_ssd212_to_uac_half_count = 0U;
    s_worker_stop = 0U;
    app_i2s_ssd212_queue_reset();

    dbg("\r\n====================================\r\n");
    dbg(" SSD212 <-> AIC8800 FULL DUPLEX I2S\r\n");
    dbg("====================================\r\n");
#if APP_SSD212_I2S_AIC_MASTER_ENABLE
    dbg("I2S_BRIDGE: AIC8800 master, SSD212 slave\r\n");
    dbg("I2S_BRIDGE: A0=LRCK OUT A1=BCK OUT A2=DIN A3=DOUT\r\n");
    dbg("I2S_BRIDGE: AIC drives LRCK=48kHz and BCK=3.072MHz\r\n");
#else
    dbg("I2S_BRIDGE: AIC8800 slave, SSD212 master\r\n");
    dbg("I2S_BRIDGE: A0=LRCK IN A1=BCK IN A2=DIN A3=DOUT\r\n");
    dbg("I2S_BRIDGE: SSD212 must drive LRCK=48kHz and BCK=3.072MHz before/while streams run\r\n");
#endif
    dbg("I2S_BRIDGE: 48kHz stereo PCM16, 24-bit word in 32-bit slot, Standard I2S\r\n");
    dbg("I2S_BRIDGE: DMA callback=5ms/240frames, Wi-Fi packet remains 5ms\r\n");
    dbg("I2S_BRIDGE: R7 capture_q=%u x 5ms playback_q=%u x 5ms worker_prio=%u periodic_diag=%s\r\n",
        (unsigned int)APP_SSD212_I2S_CAPTURE_QUEUE_DEPTH,
        (unsigned int)APP_SSD212_I2S_PLAYBACK_QUEUE_DEPTH,
        (unsigned int)APP_SSD212_I2S_WORKER_PRIO,
        (APP_SSD212_I2S_LOG_INTERVAL_MS == 0U) ? "OFF" : "ON");
#if APP_SSD212_I2S_TEST_SINE_ENABLE
    dbg("I2S_TEST: 1kHz sine ENABLED, stereo, PCM16 peak amplitude=%u; UAC playback is bypassed for I2S TX\r\n",
        (unsigned int)APP_SSD212_I2S_TEST_SINE_AMPLITUDE);
#else
    dbg("I2S_TEST: 1kHz sine DISABLED; I2S TX source is Receiver AP playback\r\n");
#endif

    app_i2s_ssd212_iomux_config();
    app_i2s_ssd212_iomux_diag("before_asio_init");

    /* In this SDK asio_init() also creates/opens the global ASIO service. */
    asio_init();
    s_asio_inited = 1U;
    app_i2s_ssd212_clock_diag("after_asio_init");

    memset(&capture_cfg, 0, sizeof(capture_cfg));
    capture_cfg.path = AUD_PATH_RX01;
    capture_cfg.device = AUD_DEVICE_EXT_CODEC_I2S0;
    capture_cfg.device_role = APP_SSD212_I2S_DEVICE_ROLE;
    capture_cfg.bits = AUD_BITS_24;       /* 24 bits + 8 extension = 32-bit slot */
    capture_cfg.ch_num = AUD_CH_NUM_2;
    capture_cfg.samp_rate = APP_SSD212_I2S_SAMPLE_RATE;
    capture_cfg.src_samp_rate = APP_SSD212_I2S_SAMPLE_RATE;
    capture_cfg.buf_ptr = s_capture_dma;
    capture_cfg.buf_size = sizeof(s_capture_dma);
    capture_cfg.handler = app_i2s_ssd212_capture_callback;
    capture_cfg.src_en = false;
    capture_cfg.eq_en = false;
    capture_cfg.mux_en = false;
    capture_cfg.vol = 0U;

    memset(&playback_cfg, 0, sizeof(playback_cfg));
    playback_cfg.path = AUD_PATH_TX01;
    playback_cfg.device = AUD_DEVICE_EXT_CODEC_I2S0;
    playback_cfg.device_role = APP_SSD212_I2S_DEVICE_ROLE;
    playback_cfg.bits = AUD_BITS_24;      /* same shared I2S clock/frame config */
    playback_cfg.ch_num = AUD_CH_NUM_2;
    playback_cfg.samp_rate = APP_SSD212_I2S_SAMPLE_RATE;
    playback_cfg.src_samp_rate = APP_SSD212_I2S_SAMPLE_RATE;
    playback_cfg.buf_ptr = s_playback_dma;
    playback_cfg.buf_size = sizeof(s_playback_dma);
    playback_cfg.handler = app_i2s_ssd212_playback_callback;
    playback_cfg.src_en = false;
    playback_cfg.eq_en = false;
    playback_cfg.mux_en = false;
    playback_cfg.vol = 0U;

    ret = asio_stream_open(APP_SSD212_I2S_STREAM_GROUP,
                           AUD_STREAM_CAPTURE,
                           &capture_cfg);
    if (ret != ASIO_ERR_NONE) {
        dbg("I2S_BRIDGE: capture open failed ret=%d\r\n", ret);
        app_i2s_ssd212_cleanup_after_start_fail();
        return -1;
    }
    s_capture_opened = 1U;

    ret = asio_stream_open(APP_SSD212_I2S_STREAM_GROUP,
                           AUD_STREAM_PLAYBACK,
                           &playback_cfg);
    if (ret != ASIO_ERR_NONE) {
        dbg("I2S_BRIDGE: playback open failed ret=%d\r\n", ret);
        app_i2s_ssd212_cleanup_after_start_fail();
        return -2;
    }
    s_playback_opened = 1U;
#if APP_SSD212_I2S_TEST_SINE_ENABLE
    ret = asio_stream_raw_digital_volume_set(
        AUD_STREAM_PLAYBACK,
        (uint8_t)APP_SSD212_I2S_TEST_PLAYBACK_VOLUME);
    dbg("I2S_TEST: playback digital volume set=0x%02x ret=%d readback=0x%02x\r\n",
        (unsigned int)APP_SSD212_I2S_TEST_PLAYBACK_VOLUME,
        ret,
        (unsigned int)asio_stream_raw_digital_volume_get(AUD_STREAM_PLAYBACK));
#endif
    app_i2s_ssd212_clock_diag("after_stream_open");

    /* Both streams share I2S0. In slave mode the SSD212 must already be
     * supplying continuous LRCK/BCK; callbacks will not advance without it. */
    ret = asio_stream_start(APP_SSD212_I2S_STREAM_GROUP,
                            AUD_STREAM_CAPTURE);
    if (ret != ASIO_ERR_NONE) {
        dbg("I2S_BRIDGE: capture start failed ret=%d\r\n", ret);
        app_i2s_ssd212_cleanup_after_start_fail();
        return -3;
    }

    /* Playback buffer is pre-cleared, so SSD212 receives silence until the
     * first UAC packets arrive. */
    ret = asio_stream_start(APP_SSD212_I2S_STREAM_GROUP,
                            AUD_STREAM_PLAYBACK);
    if (ret != ASIO_ERR_NONE) {
        dbg("I2S_BRIDGE: playback start failed ret=%d\r\n", ret);
        app_i2s_ssd212_cleanup_after_start_fail();
        return -4;
    }

    app_i2s_ssd212_iomux_config();
    app_i2s_ssd212_iomux_diag("after_stream_start");
    app_i2s_ssd212_clock_diag("after_stream_start");

    if (rtos_task_create(app_i2s_ssd212_worker,
                         "i2s_bridge",
                         APPLICATION_TASK,
                         APP_SSD212_I2S_WORKER_STACK,
                         NULL,
                         RTOS_TASK_PRIORITY(APP_SSD212_I2S_WORKER_PRIO),
                         &s_worker_task)) {
        dbg("I2S_BRIDGE: worker task create failed\r\n");
        app_i2s_ssd212_cleanup_after_start_fail();
        return -5;
    }

    s_started = 1U;
    dbg("I2S_BRIDGE: full-duplex streams started\r\n");
    return 0;
}

int app_i2s_ssd212_bridge_stop(void)
{
    uint32_t wait_ms = 0U;

    if (!s_started && !s_capture_opened && !s_playback_opened && !s_asio_inited) {
        return 0;
    }

    s_worker_stop = 1U;
    while (s_worker_running && (wait_ms < 500U)) {
        rtos_task_suspend(1U);
        wait_ms++;
    }

    if (s_capture_opened) {
        asio_stream_stop(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_CAPTURE);
        asio_stream_close(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_CAPTURE);
        s_capture_opened = 0U;
    }

    if (s_playback_opened) {
        asio_stream_stop(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_PLAYBACK);
        asio_stream_close(APP_SSD212_I2S_STREAM_GROUP, AUD_STREAM_PLAYBACK);
        s_playback_opened = 0U;
    }

    s_asio_inited = 0U;
    s_started = 0U;
    app_i2s_ssd212_queue_reset();
    dbg("I2S_BRIDGE: stopped\r\n");
    return 0;
}

uint8_t app_i2s_ssd212_bridge_is_started(void)
{
    return s_started ? 1U : 0U;
}

void app_i2s_ssd212_bridge_get_stats(app_i2s_ssd212_bridge_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
