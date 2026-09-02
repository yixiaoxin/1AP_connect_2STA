/*
 * AIC8800M40 USB receiver / SoftAP / two-triangle-microphone bridge.
 *
 * Product topology:
 *   PC USB UAC <-> Receiver AP <-> Triangle STA1 (client_id 1)
 *                              <-> Triangle STA2 (client_id 2)
 *
 * USB/playback PCM stays 48 kHz stereo PCM16; recording wire is 48 kHz stereo PCM16.
 * USB uses 1 ms / 192 bytes. Playback Wi-Fi uses 10 ms / 1920-byte PCM;
 * recording Wi-Fi normally uses one 20 ms REC20 group made from two independently BPK2-compressed 10 ms / 1920-byte native-48k subframes, both with the packed 21-byte header.
 *
 * TCP/8888: Receiver AP -> triangle playback.  Each triangle connects and
 *           first sends SESSION_HELLO on this socket.
 * TCP/8890: Triangle -> Receiver AP 48 kHz stereo recording. Each triangle connects and
 *           first sends SESSION_HELLO on this socket.
 *
 * Playback TCP/8888 and recording TCP/8890 are owned by separate nonblocking
 * tasks.  A third mixer task consumes the two independent recording jitter
 * rings and produces the USB microphone stream.  This mirrors the mature
 * four-microphone project's isolation between network directions.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "dbg.h"
#include "demo_src.h"
#include "rtos.h"
#include "rtos_al.h"
#include "plf.h"
#if PLF_WIFI_STACK && (PLF_AIC8800MC || PLF_AIC8800M40 || defined(CFG_WIFI_RAM_VER))
#include "sysctrl_api.h"
#endif
#include "fhost.h"
#include "rwnx_msg_tx.h"
#include "wlan_user.h"
#include "wlan_if.h"
#include "sleep_api.h"
#include "us_ticker_api.h"
#include "pmic_api.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"

#if PLF_WIFI_STACK
#include "ipc_host.h"
#endif

#include "uac_audio_bridge.h"

/* NETDIAG1: read-only SoftAP reorder counters exported by rwnx_rx.c. */
extern void rwnx_reord_v533_diag_snapshot(uint16_t *queued, uint16_t *peak,
                                          uint32_t *cache, uint32_t *force,
                                          uint32_t *bypass, uint32_t *old,
                                          uint32_t *low, uint32_t *limit);

#define UACM_VERSION                         "v7.0.12R18-P10-RXBYP1-LL3-BPK2-48PREP1-CPUDIAG3-UARTSAFE2-PBUF3-HOSTBUF2-JCTRL3-NETDIAG2-PLAYFIX2-REC48-REC20PPS1-DRAMFIX1"
#define UACM_WIFI_SSID                       "aic8800m40"
#define UACM_WIFI_PASSWORD                   "12345678"
#define UACM_AP_BAND                         1
#define UACM_AP_CHANNEL                      165
#define UACM_PLAYBACK_PORT                   8888
#define UACM_RECORD_PORT                     8890
#define UACM_SESSION_COUNT                   2U
#define UACM_FIRST_CLIENT_ID                 1U

#define AUDIO_PACKET_MAGIC                   0xA55A5AA5U
#define AUDIO_PACKET_TYPE_CTRL               0x02U
#define AUDIO_PACKET_TYPE_UAC_PCM            0x03U
#define AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS   0x04U
#define AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS20 0x05U
#define UACM_REC20_MAGIC                     0x5232U
#define UACM_REC20_VERSION                   1U
#define AUDIO_DIR_AP_TO_STA                  0x01U
#define AUDIO_DIR_STA_TO_AP                  0x02U
#define AUDIO_CTRL_UAC_MIC_STREAMING         0x04U
#define AUDIO_CTRL_HEARTBEAT                 0x05U
#define AUDIO_CTRL_SESSION_HELLO             0x10U

#define UACM_PCM_1MS_BYTES                   UAC_BRIDGE_USB_PACKET_BYTES
#define UACM_PCM_5MS_BYTES                   UAC_BRIDGE_TCP_PCM_BYTES
#define UACM_PLAYBACK_PACKET_MS              10U
#define UACM_PLAYBACK_PCM_BYTES              (UACM_PCM_5MS_BYTES * 2U)
#define UACM_PLAYBACK_FRAMES_10MS             \
    (UACM_PLAYBACK_PCM_BYTES / (UAC_BRIDGE_CHANNELS * sizeof(int16_t)))
#define UACM_SAMPLES_5MS                     (UACM_PCM_5MS_BYTES / sizeof(int16_t))
#define UACM_FRAMES_5MS                      (UACM_SAMPLES_5MS / UAC_BRIDGE_CHANNELS)

/* Triangle -> AP recording wire format.  REC20PPS1 sends native 48 kHz
 * stereo PCM16 in 10 ms frames.  AP therefore feeds decoded 5 ms halves
 * directly into the existing 48 kHz jitter/PLC/AutoMix/USB path; no 3x
 * upsampling is used. */
#define UACM_RECORD_SAMPLE_RATE_HZ           48000U
#define UACM_RECORD_PACKET_MS                10U /* BPK2 subframe geometry stays 10 ms */
#define UACM_RECORD_GROUP_MS                 20U
#define UACM_RECORD_GROUP_SUBFRAMES          2U
#define UACM_RECORD_GROUP_RAW_BYTES          (UACM_RECORD_PCM_10MS_BYTES * UACM_RECORD_GROUP_SUBFRAMES)
#define UACM_RECORD_BLOCKS_PER_PACKET         4U /* normal 20 ms REC20 group = four 5 ms internal blocks */
#if (UACM_RECORD_SAMPLE_RATE_HZ != 16000U) && (UACM_RECORD_SAMPLE_RATE_HZ != 48000U)
#error "UACM record wire rate must be 16000 or 48000 Hz"
#endif
#define UACM_RECORD_FRAMES_5MS               ((UACM_RECORD_SAMPLE_RATE_HZ * 5U) / 1000U)
#define UACM_RECORD_SAMPLES_5MS              (UACM_RECORD_FRAMES_5MS * UAC_BRIDGE_CHANNELS)
#define UACM_RECORD_PCM_5MS_BYTES            (UACM_RECORD_SAMPLES_5MS * sizeof(int16_t))
#define UACM_RECORD_FRAMES_10MS              ((UACM_RECORD_SAMPLE_RATE_HZ * UACM_RECORD_PACKET_MS) / 1000U)
#define UACM_RECORD_SAMPLES_10MS             (UACM_RECORD_FRAMES_10MS * UAC_BRIDGE_CHANNELS)
#define UACM_RECORD_PCM_10MS_BYTES           (UACM_RECORD_SAMPLES_10MS * sizeof(int16_t))

#define UACM_LOSSLESS_MAGIC                   0x4C52U
#define UACM_LOSSLESS_VERSION                 4U
#define UACM_LOSSLESS_BLOCK_FRAMES            64U
#define UACM_LOSSLESS_MAX_WIDTH               17U
#define UACM_LOSSLESS_DESC_RAW                 0x80U
#define UACM_LOSSLESS_DESC_WIDTH_MASK          0x1FU
#define UACM_LOSSLESS_LOG_MS                 30000U /* deferred snapshot cadence label; no long print while audio active */
#define UACM_LOSSLESS_PLAY_WARN_US             1500U
#define UACM_LOSSLESS_RECORD_WARN_US           1000U
/* PBUF3: duplicate codec timing probes are disabled.  The authoritative
 * encode/decode pass and per-frame CRC verification remain unchanged. */
#define UACM_CODEC_PROBE_EVERY                    0U
#define UACM_LLCHK_LOG_MS                    67000U /* short record integrity line; de-phased from 5 s AUDIO */

/* R8M keeps the mature-project recording architecture but returns the large
 * static PCM arrays to the proven R6 DRAM budget.  The linker report from the
 * target showed R8 overflowing DRAM by 22,728 bytes.  Reducing both recording
 * rings from 80 ms to 25 ms saves 21,120 bytes; reducing the USB microphone
 * ring from 30 ms to 10 ms saves another 3,840 bytes (24,960 bytes total).
 *
 * The important mature-project mechanisms remain unchanged:
 *   - playback and recording use independent tasks;
 *   - T1/T2 have independent jitter-ring locks;
 *   - sequence-gap PLC, partial-block fade, empty-block concealment and
 *     239/240/241-frame drift correction remain enabled;
 *   - STA-side 20 ms unsent-frame stale drop remains unchanged.
 *
 * USB speaker source: 15 ms
 * playback per STA:   10 ms
 * recording per mic:  25 ms
 * USB microphone out: 10 ms
 * record prebuffer:    15 ms
 * record trim:         above 20 ms, keep latest 15 ms
 *
 * Total static PCM ring storage returns to 25,440 bytes, matching the R6
 * budget while preserving the new R8 state machines and task separation. */
#define UACM_USB_SPK_RING_BYTES              (UACM_PCM_1MS_BYTES * 15U)
#define UACM_SESSION_TX_RING_BYTES           UACM_PLAYBACK_PCM_BYTES /* one 10 ms playback packet */
#define UACM_SESSION_RX_RING_BYTES           (UACM_PCM_5MS_BYTES * 4U)  /* 20 ms per-source record jitter ring; DRAMFIX1 saves 960B/session */
#define UACM_USB_MIC_RING_BYTES              (UACM_PCM_1MS_BYTES * 10U) /* 10 ms, proven R6 USB output budget */
#define UACM_RX_PREBUFFER_BYTES              (UACM_PCM_5MS_BYTES * 3U)  /* 15 ms startup/rebuffer target */
#define UACM_RX_MAX_DELAY_BYTES              (UACM_PCM_5MS_BYTES * 4U)  /* 20 ms control threshold */
#define UACM_RX_TRIM_KEEP_BYTES              (UACM_PCM_5MS_BYTES * 3U)  /* retain latest 15 ms */
#define UACM_RX_PLC_MAX_LOST_PACKETS         2U
#define UACM_RX_CONCEAL_EMPTY_BLOCKS         8U  /* 40 ms at 5 ms/block, same duration as mature project */
#define UACM_RX_DRIFT_CHECK_BLOCKS           40U /* about 200 ms */
#define UACM_RX_DRIFT_DEADBAND_FRAMES        192U /* 4 ms at 48 kHz */

#define UACM_PLAYBACK_WIRE_FRAME_BYTES       (sizeof(audio_header_t) + UACM_PLAYBACK_PCM_BYTES)
#define UACM_RECORD_RX_STREAM_BYTES           (sizeof(audio_header_t) + UACM_RECORD_PCM_10MS_BYTES) /* DRAMFIX1: one complete max-size record frame; parser drains before next recv */
#define UACM_NET_TASK_STACK                  4096U
#define UACM_RECORD_TASK_STACK               4096U
#define UACM_MIX_TASK_STACK                  2560U
#define UACM_NET_TASK_PRIO                   3U
#define UACM_RECORD_TASK_PRIO                4U
#define UACM_MIX_TASK_PRIO                   3U
#define UACM_NET_SLEEP_MS                    1U
/* Mixer pacing is driven by free space in the USB microphone ring.
 * Poll at 1 ms resolution, but generate a 5 ms block only when the USB
 * consumer has freed room for that complete block.  This removes the
 * processing-time drift caused by sleeping a fixed 5 ms after each mix. */
#define UACM_MIX_POLL_MS                     1U
#define UACM_USB_MIC_PRODUCE_BYTES           UACM_PCM_5MS_BYTES
#define UACM_SOCKET_BUF_BYTES                (4U * 1024U)
#define UACM_RECORD_SOCKET_BUF_BYTES         (2U * 1024U)
/* PLAYFIX2: preserve the mature high-priority record task, but bound how
 * much RX work it may do in one scheduler pass while playback is also
 * active.  REC20 normally produces one 20 ms record application packet (50 pps) per STA, so
 * servicing one STA per 1 ms pass with two nonblocking recv attempts is
 * still well above the required drain rate while preventing an RX backlog
 * from monopolizing priority-4 CPU time ahead of playback/TCPIP. */
#define UACM_RECORD_RX_BUDGET_READS          8U
#define UACM_RECORD_RX_FD_BUDGET_READS       2U
#define UACM_RECORD_RX_FD_SESSIONS_PER_LOOP  1U
#define UACM_HELLO_TIMEOUT_MS                200U
#define UACM_AGG_REAPPLY_GUARD_MS           10000U
#define UACM_TX_BACKPRESSURE_TIMEOUT_MS      500U
#define UACM_TX_ENOMEM_RETRY_LIMIT          32U
#define UACM_TX_RECOVERY_COOLDOWN_MS        5000U
#define UACM_LOG_RED                        "\033[31m"
#define UACM_LOG_RESET                      "\033[0m"
#define UACM_HEARTBEAT_MS                    1000U
#define UACM_AUDIO_LOG_MS                     5000U
#define UACM_MIX_DIAG_MS                     5000U
/* R19-AUTOREBOOT1: whole-chip soft-reboot watchdog.  If a connected playback
 * stream stays below target PPS for a sustained window the AP is considered
 * wedged (e.g. TCP/lwIP stuck) and we reboot to recover the audio chain.
 * The window mirrors UACM_AUDIO_LOG_MS so the check runs once per AUDIO log. */
#define UACM_AUTO_REBOOT_ABNORMAL_MS         5000U
#define UACM_PLAY_EXPECTED_PPS               100U
#define UACM_RECORD_EXPECTED_PPS             50U
#define UACM_PLAY_ENQUEUE_IDLE_BUDGET        4U
#define UACM_PLAY_ENQUEUE_FD_BUDGET          1U
#define UACM_AP_AUDIO_STA_COUNT              2U

#define UACM_TX_KIND_NONE                    0U
#define UACM_TX_KIND_PCM                     1U
#define UACM_TX_KIND_CTRL                    2U

#define UACM_VAD_HOLD_BLOCKS                 40U   /* 200 ms */
#define UACM_VAD_NOISE_INIT_POWER            10000ULL
#define UACM_VAD_OPEN_MIN_POWER              40000ULL
#define UACM_VAD_CLOSE_MIN_POWER             22500ULL
#define UACM_GAIN_ONE_Q15                    32768U
#define UACM_GAIN_MIN_DUAL_Q15               4096U
#define UACM_GAIN_MAX_DUAL_Q15               28672U

/* 5 ms block-boundary click protection.  A trim, rebuffer, drift correction
 * or source transition can make the first sample of a new block differ sharply
 * from the previous block's last sample.  Instead of hard-limiting every sample
 * (which can itself create high-frequency edges), correct only a genuinely
 * abnormal block boundary and release that correction smoothly over 1 ms.
 * The microphone low-pass is applied afterwards as the final waveform stage. */
#define UACM_BOUNDARY_DECLICK_THRESHOLD       3072
#define UACM_BOUNDARY_DECLICK_FRAMES          \
    (UACM_PCM_1MS_BYTES / UAC_BRIDGE_FRAME_BYTES)
#define UACM_BOUNDARY_DECLICK_DENOM            \
    (UACM_BOUNDARY_DECLICK_FRAMES - 1U)

/* Legacy 16->48 kHz microphone anti-imaging low-pass.  REC20PPS1 receives
 * native 48 kHz and bypasses this FIR; it remains compiled for the 16 kHz
 * configuration so the older path can still be restored.
 * Linear 3x interpolation leaves spectral images above the source Nyquist
 * frequency.  Apply one shared, phase-continuous 31-tap FIR after AutoMix and
 * after boundary correction, so the FIR is the final waveform-shaping stage.
 * The Q14 coefficients have unity DC
 * gain; response is approximately -1.4 dB at 6 kHz, -4.9 dB at 7 kHz,
 * -12.2 dB at 8 kHz and at least -25 dB from 9 kHz upward.
 *
 * The FIR is symmetric, so each output sample needs only 16 multiplies per
 * channel.  One shared state block adds only about 128 bytes of static DRAM. */
#define UACM_MIC_LPF_TAPS                     31U
#define UACM_MIC_LPF_HALF_TAPS                (UACM_MIC_LPF_TAPS / 2U)
#define UACM_MIC_LPF_Q                        14U
#define UACM_MIC_LPF_ROUND                    (1L << (UACM_MIC_LPF_Q - 1U))
#define UACM_MIC_LPF_CUTOFF_HZ                7200U

/* USB microphone underrun smoothing.  The mixer produces 5 ms blocks while
 * TinyUSB consumes 1 ms packets.  If the consumer catches the producer, the
 * original R18 ISR inserted an abrupt 1 ms all-zero packet, creating two
 * broadband edges (audio->zero and zero->audio).  Fade the first underrun
 * packet to zero, wait for one complete 5 ms producer block, then fade the
 * first recovered packet back in.  Existing ring storage is reused. */
#define UACM_USB_MIC_REBUFFER_MS              5U
#define UACM_USB_MIC_REBUFFER_BYTES           \
    (UACM_PCM_1MS_BYTES * UACM_USB_MIC_REBUFFER_MS)
#define UACM_USB_MIC_FADE_FRAMES              \
    (UACM_PCM_1MS_BYTES / UAC_BRIDGE_FRAME_BYTES)

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t seq_num;
    uint64_t timestamp;
    uint8_t  packet_type;
    uint8_t  direction;
    uint8_t  client_id;
    uint16_t data_len;
} audio_header_t;

typedef struct {
    uint8_t  ctrl_type;
    uint8_t  value;
    uint16_t reserved;
} audio_ctrl_payload_t;

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  block_frames;
    uint16_t raw_len;
    uint8_t  block_count;
    uint8_t  reserved;
    uint32_t raw_crc32;
} uacm_lossless_header_t;

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  subframes;
    uint16_t len0;
    uint16_t len1;
} uacm_rec20_header_t;
#pragma pack(pop)

typedef char uacm_header_size_check[(sizeof(audio_header_t) == 21U) ? 1 : -1];
typedef char uacm_ctrl_size_check[(sizeof(audio_ctrl_payload_t) == 4U) ? 1 : -1];
typedef char uacm_lossless_header_size_check[(sizeof(uacm_lossless_header_t) == 12U) ? 1 : -1];
typedef char uacm_rec20_header_size_check[(sizeof(uacm_rec20_header_t) == 8U) ? 1 : -1];
typedef char uacm_playback_10ms_size_check[(UACM_PLAYBACK_PCM_BYTES == 1920U) ? 1 : -1];
typedef char uacm_record_rate_math_check[((UACM_RECORD_FRAMES_10MS * UAC_BRIDGE_CHANNELS * sizeof(int16_t)) == UACM_RECORD_PCM_10MS_BYTES) ? 1 : -1];
typedef char uacm_record_48k_frame_check[(UACM_RECORD_FRAMES_10MS == 480U) ? 1 : -1];
typedef char uacm_record_48k_byte_check[(UACM_RECORD_PCM_10MS_BYTES == 1920U) ? 1 : -1];
typedef char uacm_playback_frame_count_check[(UACM_PLAYBACK_FRAMES_10MS == 480U) ? 1 : -1];

typedef struct {
    uint8_t *storage;
    uint32_t capacity;
    volatile uint32_t read_pos;
    volatile uint32_t write_pos;
} uacm_ring_t;

typedef struct {
    int16_t last_sample[UAC_BRIDGE_CHANNELS];
    uint8_t valid;
} uacm_boundary_state_t;

typedef struct {
    int16_t history[UAC_BRIDGE_CHANNELS][UACM_MIC_LPF_TAPS];
    uint8_t write_pos;
    uint8_t valid;
} uacm_mic_lpf_state_t;

/* 31-tap Hamming-window low-pass, Fs=48 kHz, nominal cutoff=7.2 kHz.
 * Coefficients are symmetric Q14 and sum exactly to 16384. */
static const int16_t s_uacm_mic_lpf_q14[UACM_MIC_LPF_TAPS] = {
      28,   20,  -15,  -69,  -89,    0,  186,  304,
     135, -348, -802, -649,  489, 2377, 4170, 4910,
    4170, 2377,  489, -649, -802, -348,  135,  304,
     186,    0,  -89,  -69,  -15,   20,   28
};

typedef struct {
    uint8_t client_id;
    int playback_fd;
    int record_fd;

    uacm_ring_t tx_ring;
    uacm_ring_t rx_ring;
    rtos_mutex rx_mutex; /* mature-project per-source jitter-ring ownership */

    uint8_t tx_wire[UACM_PLAYBACK_WIRE_FRAME_BYTES] __attribute__((aligned(4)));
    uint16_t tx_len;
    uint16_t tx_off;
    uint8_t tx_kind;
    uint8_t tx_lossless;
    uint16_t tx_payload_len;
    uint32_t tx_seq;
    uint32_t tx_progress_ms;
    uint32_t last_tx_ms;
    uint32_t tx_blocked_since_ms;
    uint32_t tx_last_errno;
    uint32_t tx_backpressure_count;
    volatile uint8_t ctrl_pending;
    volatile uint8_t ctrl_mic_on;

    uint8_t rx_stream[UACM_RECORD_RX_STREAM_BYTES] __attribute__((aligned(4)));
    uint16_t rx_len;
    uint8_t rx_seq_valid;
    uint32_t rx_last_seq;

    uint8_t rx_started;
    uint8_t rx_conceal_blocks;
    uint8_t rx_last_output_valid;
    uint8_t rx_last_input_valid;
    uint8_t rx_16k_prev_valid;
    int16_t rx_last_output[UAC_BRIDGE_CHANNELS];
    int16_t rx_last_input[UAC_BRIDGE_CHANNELS];
    int16_t rx_16k_prev[UAC_BRIDGE_CHANNELS];
    uint32_t rx_drift_level_sum;
    uint32_t rx_drift_block_count;
    uint32_t rx_drift_drop_frames;
    uint32_t rx_drift_repeat_frames;
    uint32_t rx_plc_frames;
    uint32_t rx_rebuffer_count;

    uint8_t active;
    uint8_t active_hold;
    uint64_t noise_power;
    uint64_t level_power;
    uint32_t gain_q15;

    uint32_t playback_packets;
    uint32_t playback_drop;
    uint32_t play_lossless_comp_packets;
    uint32_t play_lossless_raw_packets;
    uint32_t play_lossless_payload_bytes;
    uint16_t play_lossless_payload_min;
    uint16_t play_lossless_payload_max;
    uint32_t play_lossless_encode_max_us;
    uint32_t record_packets;      /* internal 5 ms blocks */
    uint32_t record_wire_packets; /* received record application packets: normally 20 ms REC20 */
    uint32_t record_group20_packets;
    uint32_t record_fallback10_packets;
    uint32_t lossless_raw_equiv_bytes;
    uint32_t lossless_comp_packets;
    uint32_t lossless_raw_packets;
    uint32_t lossless_payload_bytes;
    uint32_t lossless_crc_fail;
    uint32_t lossless_decode_fail;
    uint32_t lossless_len_fail;
    uint32_t lossless_verify_ok;
    uint32_t lossless_decode_max_us;
    uint32_t lossless_decode_slow_hits;
    uint32_t lossless_decode_probe_est_max_us;
    uint32_t lossless_decode_probe_count;
    uint32_t lossless_decode_probe_fail;
    uint32_t record_drop;         /* internal 5 ms blocks */
    uint32_t seq_gap;
    uint32_t seq_old;
    uint32_t underflow;
    uint32_t reconnects;

    /* Log state: report transitions once instead of printing every retry. */
    uint8_t playback_seen;
    uint8_t record_seen;
    uint8_t playback_down_reported;
    uint8_t record_down_reported;
} uacm_session_t;

static uint8_t s_usb_spk_storage[UACM_USB_SPK_RING_BYTES] __attribute__((aligned(4)));
static uint8_t s_usb_mic_storage[UACM_USB_MIC_RING_BYTES] __attribute__((aligned(4)));
static uint8_t s_tx_storage[UACM_SESSION_COUNT][UACM_SESSION_TX_RING_BYTES] __attribute__((aligned(4)));
static uint8_t s_rx_storage[UACM_SESSION_COUNT][UACM_SESSION_RX_RING_BYTES] __attribute__((aligned(4)));
static uacm_ring_t s_usb_spk_ring = { s_usb_spk_storage, UACM_USB_SPK_RING_BYTES, 0U, 0U };
static uacm_ring_t s_usb_mic_ring = { s_usb_mic_storage, UACM_USB_MIC_RING_BYTES, 0U, 0U };
static uacm_session_t s_session[UACM_SESSION_COUNT];
static uint32_t s_play_lossless_slow_hits;
static uint32_t s_play_lossless_shared_frames;
static uint32_t s_play_lossless_encode_calls;
static uint32_t s_play_lossless_probe_est_max_us;
static uint32_t s_play_lossless_probe_count;
static uint32_t s_play_lossless_probe_fail;
/* LL2-DRAMFIX1: do not reserve extra global PCM scratch for the lossless
 * codec.  The previous LL2 added 1920 B playback + 640 B record scratch in
 * .uninited/.bss and overflowed the target DRAM by 1488 B.  Both buffers now
 * live inside the already-provisioned 4096 B network/record task stacks. */
static uacm_mic_lpf_state_t s_mic_lpf_state;

static volatile uint8_t s_usb_mic_on;
static volatile uint8_t s_ap_rx_agg_update_pending;
static uint8_t s_ap_rx_agg_disabled;
static uint32_t s_ap_rx_agg_last_apply_ms;
static volatile uint8_t s_usb_spk_on;
static volatile uint8_t s_usb_mic_resetting;
static volatile uint8_t s_usb_spk_resetting;
static volatile int16_t s_usb_spk_volume_raw;
static volatile uint8_t s_usb_spk_mute;

static uint32_t s_usb_spk_packets;
static uint32_t s_usb_spk_drop;
static uint32_t s_usb_mic_packets;
static uint32_t s_usb_mic_silence;
static uint32_t s_usb_mic_drop;
static uint32_t s_mix_blocks;
static uint32_t s_mix_zero_blocks;
static uint32_t s_mix_dual_blocks;
static uint32_t s_mix_fallback_blocks;

/* USB microphone output state is owned by the 1 ms render ISR.  The reset
 * path changes it only while s_usb_mic_resetting is asserted. */
static volatile uint8_t s_usb_mic_rebuffering = 1U;
static uint8_t s_usb_mic_last_output_valid;
static int16_t s_usb_mic_last_output[UAC_BRIDGE_CHANNELS];
static uint32_t s_usb_mic_underflow_events;
static uint32_t s_usb_mic_rebuffer_wait_packets;
static uint32_t s_usb_mic_recoveries;

static uint64_t uacm_time_us(void)
{
    static uint32_t last_tick;
    static uint64_t wrap_base;
    uint32_t protect = rtos_protect();
    uint32_t now = us_ticker_read();
    uint64_t result;

    /* Keep the useful R12 concurrency fix while restoring R11 radio policy. */
    if ((now < last_tick) && ((last_tick - now) > 0x80000000U)) {
        wrap_base += 0x100000000ULL;
    }
    last_tick = now;
    result = wrap_base + now;
    rtos_unprotect(protect);
    return result;
}

static uint32_t uacm_now_ms(void)
{
    return (uint32_t)(uacm_time_us() / 1000ULL);
}

#define UACM_FLOW_OFF                        0U
#define UACM_FLOW_DOWN                       1U
#define UACM_FLOW_ACTIVE                     2U

static uint32_t uacm_rate_per_second(uint32_t delta, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0U) {
        return 0U;
    }
    return (uint32_t)((((uint64_t)delta * 1000ULL) +
                       ((uint64_t)elapsed_ms / 2ULL)) /
                      (uint64_t)elapsed_ms);
}

static uint32_t uacm_effective_packets(uint32_t completed,
                                       uint32_t lost,
                                       uint32_t expected)
{
    uint32_t maximum;
    if (lost >= expected) {
        return 0U;
    }
    maximum = expected - lost;
    if (completed > maximum) {
        completed = maximum;
    }
    return completed;
}

static uint32_t uacm_append_u32(char *dst, uint32_t dst_size,
                                uint32_t pos, uint32_t value)
{
    char reverse[10];
    uint32_t count = 0U;

    do {
        reverse[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(reverse)));

    while ((count != 0U) && ((pos + 1U) < dst_size)) {
        dst[pos++] = reverse[--count];
    }
    return pos;
}

static uint32_t uacm_append_text(char *dst, uint32_t dst_size,
                                 uint32_t pos, const char *text)
{
    while ((*text != '\0') && ((pos + 1U) < dst_size)) {
        dst[pos++] = *text++;
    }
    return pos;
}

static void uacm_format_flow(char *dst, uint32_t dst_size,
                             uint8_t state, uint32_t actual,
                             uint32_t expected)
{
    uint32_t pos = 0U;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (state == UACM_FLOW_OFF) {
        pos = uacm_append_text(dst, dst_size, pos, "OFF");
    } else if (state == UACM_FLOW_DOWN) {
        pos = uacm_append_text(dst, dst_size, pos, "DOWN");
    } else {
        pos = uacm_append_u32(dst, dst_size, pos, actual);
        pos = uacm_append_text(dst, dst_size, pos, "/");
        pos = uacm_append_u32(dst, dst_size, pos, expected);
    }
    dst[pos] = '\0';
}

static inline void uacm_barrier(void)
{
    __sync_synchronize();
}

static uint32_t uacm_ring_used(const uacm_ring_t *ring)
{
    uint32_t wr = ring->write_pos;
    uacm_barrier();
    return wr - ring->read_pos;
}

static uint32_t uacm_ring_free(const uacm_ring_t *ring)
{
    uint32_t used = uacm_ring_used(ring);
    return (used < ring->capacity) ? (ring->capacity - used) : 0U;
}

static void uacm_ring_reset(uacm_ring_t *ring)
{
    ring->read_pos = 0U;
    uacm_barrier();
    ring->write_pos = 0U;
}

static int uacm_ring_write(uacm_ring_t *ring, const uint8_t *src, uint32_t len)
{
    uint32_t wr;
    uint32_t first;

    if ((src == NULL) || (len == 0U) || (len > ring->capacity) ||
        (uacm_ring_free(ring) < len)) {
        return -1;
    }

    wr = ring->write_pos % ring->capacity;
    first = ring->capacity - wr;
    if (first > len) {
        first = len;
    }
    memcpy(ring->storage + wr, src, first);
    if (len > first) {
        memcpy(ring->storage, src + first, len - first);
    }
    uacm_barrier();
    ring->write_pos += len;
    return 0;
}

static uint32_t uacm_ring_read(uacm_ring_t *ring, uint8_t *dst, uint32_t len)
{
    uint32_t used = uacm_ring_used(ring);
    uint32_t rd;
    uint32_t first;

    if (len > used) {
        len = used;
    }
    if (len == 0U) {
        return 0U;
    }

    rd = ring->read_pos % ring->capacity;
    first = ring->capacity - rd;
    if (first > len) {
        first = len;
    }
    if (dst != NULL) {
        memcpy(dst, ring->storage + rd, first);
        if (len > first) {
            memcpy(dst + first, ring->storage, len - first);
        }
    }
    uacm_barrier();
    ring->read_pos += len;
    return len;
}

static uint32_t uacm_ring_peek_copy(const uacm_ring_t *ring, uint8_t *dst, uint32_t len)
{
    uint32_t used = uacm_ring_used(ring);
    uint32_t rd, first;
    if ((dst == NULL) || (len > used)) return 0U;
    rd = ring->read_pos % ring->capacity;
    first = ring->capacity - rd;
    if (first > len) first = len;
    memcpy(dst, ring->storage + rd, first);
    if (len > first) memcpy(dst + first, ring->storage, len - first);
    return len;
}

static int uacm_ring_peek_equal(const uacm_ring_t *ring, const uint8_t *src, uint32_t len)
{
    uint32_t used = uacm_ring_used(ring);
    uint32_t rd, first;
    if ((src == NULL) || (len > used)) return 0;
    rd = ring->read_pos % ring->capacity;
    first = ring->capacity - rd;
    if (first > len) first = len;
    if (memcmp(ring->storage + rd, src, first) != 0) return 0;
    if ((len > first) && (memcmp(ring->storage, src + first, len - first) != 0)) return 0;
    return 1;
}

static void uacm_ring_keep_latest(uacm_ring_t *ring, const uint8_t *src,
                                  uint32_t len, uint32_t discard_quantum,
                                  uint32_t *drop_counter)
{
    while ((uacm_ring_free(ring) < len) &&
           (uacm_ring_used(ring) >= discard_quantum)) {
        (void)uacm_ring_read(ring, NULL, discard_quantum);
        if (drop_counter != NULL) {
            (*drop_counter)++;
        }
    }
    if (uacm_ring_write(ring, src, len) != 0) {
        if (drop_counter != NULL) {
            (*drop_counter)++;
        }
    }
}

static void uacm_reset_usb_speaker(void)
{
    s_usb_spk_resetting = 1U;
    uacm_barrier();
    uacm_ring_reset(&s_usb_spk_ring);
    uacm_barrier();
    s_usb_spk_resetting = 0U;
}

static void uacm_reset_usb_microphone(void)
{
    s_usb_mic_resetting = 1U;
    uacm_barrier();
    uacm_ring_reset(&s_usb_mic_ring);
    s_usb_mic_rebuffering = 1U;
    s_usb_mic_last_output_valid = 0U;
    s_usb_mic_last_output[0] = 0;
    s_usb_mic_last_output[1] = 0;
    uacm_barrier();
    s_usb_mic_resetting = 0U;
}

int uac_bridge_usb_speaker_push_isr(const uint8_t *pcm, uint32_t len)
{
    s_usb_spk_packets++;
    if ((pcm == NULL) || (len != UACM_PCM_1MS_BYTES) ||
        !s_usb_spk_on || s_usb_spk_resetting) {
        s_usb_spk_drop++;
        return -1;
    }
    if (uacm_ring_write(&s_usb_spk_ring, pcm, len) != 0) {
        s_usb_spk_drop++;
        return -1;
    }
    return 0;
}

static int16_t uacm_pcm16_load(const uint8_t *pcm, uint32_t sample_index)
{
    int16_t value;
    memcpy(&value, pcm + sample_index * sizeof(int16_t), sizeof(value));
    return value;
}

static void uacm_pcm16_store(uint8_t *pcm, uint32_t sample_index,
                             int16_t value)
{
    memcpy(pcm + sample_index * sizeof(int16_t), &value, sizeof(value));
}

static void uacm_usb_mic_remember_tail(const uint8_t *pcm)
{
    uint32_t ch;
    uint32_t first = (UACM_USB_MIC_FADE_FRAMES - 1U) *
                     UAC_BRIDGE_CHANNELS;

    for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
        s_usb_mic_last_output[ch] = uacm_pcm16_load(pcm, first + ch);
    }
    s_usb_mic_last_output_valid = 1U;
}

/* Generate one 1 ms concealment packet that starts at the last sample sent to
 * USB and reaches zero at the end.  This removes the audio->zero edge. */
static void uacm_usb_mic_fade_out_to_zero(uint8_t *dst)
{
    uint32_t frame;
    uint32_t ch;

    if (!s_usb_mic_last_output_valid ||
        (UACM_USB_MIC_FADE_FRAMES < 2U)) {
        memset(dst, 0, UACM_PCM_1MS_BYTES);
    } else {
        for (frame = 0U; frame < UACM_USB_MIC_FADE_FRAMES; frame++) {
            uint32_t remain = UACM_USB_MIC_FADE_FRAMES - 1U - frame;
            for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
                int32_t value =
                    ((int32_t)s_usb_mic_last_output[ch] * (int32_t)remain) /
                    (int32_t)(UACM_USB_MIC_FADE_FRAMES - 1U);
                uacm_pcm16_store(dst,
                    frame * UAC_BRIDGE_CHANNELS + ch, (int16_t)value);
            }
        }
    }

    s_usb_mic_last_output[0] = 0;
    s_usb_mic_last_output[1] = 0;
    s_usb_mic_last_output_valid = 1U;
}

/* The first real packet after rebuffering is amplitude-ramped from zero to
 * its original level over 1 ms.  This removes the zero->audio edge. */
static void uacm_usb_mic_fade_in_from_zero(uint8_t *pcm)
{
    uint32_t frame;
    uint32_t ch;

    for (frame = 0U; frame < UACM_USB_MIC_FADE_FRAMES; frame++) {
        uint32_t gain = frame + 1U;
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            uint32_t index = frame * UAC_BRIDGE_CHANNELS + ch;
            int32_t original = uacm_pcm16_load(pcm, index);
            int32_t value = (original * (int32_t)gain) /
                            (int32_t)UACM_USB_MIC_FADE_FRAMES;
            uacm_pcm16_store(pcm, index, (int16_t)value);
        }
    }
    uacm_usb_mic_remember_tail(pcm);
}

static void uacm_usb_mic_output_zero(uint8_t *dst)
{
    memset(dst, 0, UACM_PCM_1MS_BYTES);
    s_usb_mic_last_output[0] = 0;
    s_usb_mic_last_output[1] = 0;
    s_usb_mic_last_output_valid = 1U;
}

int uac_bridge_usb_mic_render_isr(uint8_t *dst, uint32_t len)
{
    uint32_t used;

    s_usb_mic_packets++;
    if ((dst == NULL) || (len != UACM_PCM_1MS_BYTES)) {
        return 0;
    }

    if (!s_usb_mic_on || s_usb_mic_resetting) {
        memset(dst, 0, len);
        s_usb_mic_silence++;
        return 0;
    }

    used = uacm_ring_used(&s_usb_mic_ring);

    /* After an underrun, do not alternate between one real packet and one
     * zero packet.  Wait for the next complete 5 ms producer block. */
    if (s_usb_mic_rebuffering) {
        if (used < UACM_USB_MIC_REBUFFER_BYTES) {
            uacm_usb_mic_output_zero(dst);
            s_usb_mic_silence++;
            s_usb_mic_rebuffer_wait_packets++;
            return 0;
        }
        if (uacm_ring_read(&s_usb_mic_ring, dst, len) != len) {
            uacm_usb_mic_output_zero(dst);
            s_usb_mic_silence++;
            s_usb_mic_rebuffer_wait_packets++;
            return 0;
        }

        uacm_usb_mic_fade_in_from_zero(dst);
        s_usb_mic_rebuffering = 0U;
        s_usb_mic_recoveries++;
        return 1;
    }

    if (used < len) {
        uacm_usb_mic_fade_out_to_zero(dst);
        s_usb_mic_rebuffering = 1U;
        s_usb_mic_underflow_events++;
        s_usb_mic_silence++;
        return 0;
    }

    if (uacm_ring_read(&s_usb_mic_ring, dst, len) != len) {
        uacm_usb_mic_fade_out_to_zero(dst);
        s_usb_mic_rebuffering = 1U;
        s_usb_mic_underflow_events++;
        s_usb_mic_silence++;
        return 0;
    }

    uacm_usb_mic_remember_tail(dst);
    return 1;
}

void uac_bridge_usb_stream_state(uint8_t mic_on, uint8_t speaker_on)
{
    uint32_t i;
    uint8_t new_mic = mic_on ? 1U : 0U;
    uint8_t new_spk = speaker_on ? 1U : 0U;

    if (new_mic != s_usb_mic_on) {
        s_usb_mic_on = new_mic;
        uacm_reset_usb_microphone();
        for (i = 0U; i < UACM_SESSION_COUNT; i++) {
            s_session[i].ctrl_mic_on = new_mic;
            s_session[i].ctrl_pending = 1U;
        }
        /* R11 keeps record RX AMPDU enabled.  R10 disabled aggregation on
         * both ends of the 400 frame/s record uplink and the logs then showed
         * periodic lwIP/host-buffer backpressure.  Apply the enabled state
         * outside the TinyUSB callback context. */
        s_ap_rx_agg_update_pending = 1U;
    }
    if (new_spk != s_usb_spk_on) {
        s_usb_spk_on = new_spk;
        uacm_reset_usb_speaker();
    }
}

void uac_bridge_usb_playback_ctrl(int16_t volume_raw, uint8_t mute)
{
    s_usb_spk_volume_raw = volume_raw;
    s_usb_spk_mute = mute ? 1U : 0U;
}

static void uacm_session_audio_reset(uacm_session_t *s)
{
    uacm_ring_reset(&s->tx_ring);
    uacm_ring_reset(&s->rx_ring);
    s->tx_len = 0U;
    s->tx_off = 0U;
    s->tx_kind = UACM_TX_KIND_NONE;
    s->rx_len = 0U;
    s->rx_seq_valid = 0U;
    s->rx_started = 0U;
    s->rx_conceal_blocks = 0U;
    s->rx_last_output_valid = 0U;
    s->rx_last_input_valid = 0U;
    s->rx_16k_prev_valid = 0U;
    s->rx_last_output[0] = s->rx_last_output[1] = 0;
    s->rx_last_input[0] = s->rx_last_input[1] = 0;
    s->rx_16k_prev[0] = s->rx_16k_prev[1] = 0;
    s->rx_drift_level_sum = 0U;
    s->rx_drift_block_count = 0U;
    s->rx_drift_drop_frames = 0U;
    s->rx_drift_repeat_frames = 0U;
    s->rx_plc_frames = 0U;
    s->rx_rebuffer_count = 0U;
    s->active = 0U;
    s->active_hold = 0U;
    s->noise_power = UACM_VAD_NOISE_INIT_POWER;
    s->level_power = UACM_VAD_NOISE_INIT_POWER;
    s->gain_q15 = 0U;
}

static void uacm_close_fd(int *fd)
{
    if ((fd != NULL) && (*fd >= 0)) {
        (void)shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

static void uacm_close_playback(uacm_session_t *s)
{
    uacm_close_fd(&s->playback_fd);
    uacm_ring_reset(&s->tx_ring);
    s->tx_len = 0U;
    s->tx_off = 0U;
    s->tx_kind = UACM_TX_KIND_NONE;
    s->tx_lossless = 0U;
    s->tx_payload_len = 0U;
    s->tx_progress_ms = 0U;
    s->last_tx_ms = 0U;
    s->tx_blocked_since_ms = 0U;
    s->tx_last_errno = 0U;
    s->tx_backpressure_count = 0U;
}

static void uacm_restart_playback_service(int *listen_fd)
{
    uint32_t i;

    dbg("UACM RECOVERY restart playback service reason=ENOMEM\n");
    for (i = 0U; i < UACM_SESSION_COUNT; i++) {
        uacm_close_playback(&s_session[i]);
    }
    uacm_close_fd(listen_fd);
}

static void uacm_rx_lock(uacm_session_t *s)
{
    if ((s != NULL) && (s->rx_mutex != NULL)) {
        rtos_mutex_lock(s->rx_mutex, -1);
    }
}

static void uacm_rx_unlock(uacm_session_t *s)
{
    if ((s != NULL) && (s->rx_mutex != NULL)) {
        rtos_mutex_unlock(s->rx_mutex);
    }
}

static void uacm_close_record(uacm_session_t *s)
{
    uacm_close_fd(&s->record_fd);
    uacm_rx_lock(s);
    uacm_ring_reset(&s->rx_ring);
    s->rx_len = 0U;
    s->rx_seq_valid = 0U;
    s->rx_started = 0U;
    s->rx_conceal_blocks = 0U;
    s->rx_last_output_valid = 0U;
    s->rx_last_input_valid = 0U;
    s->rx_16k_prev_valid = 0U;
    s->rx_drift_level_sum = 0U;
    s->rx_drift_block_count = 0U;
    s->active = 0U;
    s->active_hold = 0U;
    uacm_rx_unlock(s);
}

static void uacm_init_sessions(void)
{
    uint32_t i;
    memset(s_session, 0, sizeof(s_session));
    for (i = 0U; i < UACM_SESSION_COUNT; i++) {
        uacm_session_t *s = &s_session[i];
        s->client_id = (uint8_t)(UACM_FIRST_CLIENT_ID + i);
        s->playback_fd = -1;
        s->record_fd = -1;
        s->tx_ring.storage = s_tx_storage[i];
        s->tx_ring.capacity = UACM_SESSION_TX_RING_BYTES;
        s->rx_ring.storage = s_rx_storage[i];
        s->rx_ring.capacity = UACM_SESSION_RX_RING_BYTES;
        if (rtos_mutex_create(&s->rx_mutex) != 0) {
            s->rx_mutex = NULL;
            dbg("UACM ERROR triangle%u record jitter mutex create failed\n",
                (unsigned)s->client_id);
        }
        s->ctrl_mic_on = s_usb_mic_on;
        s->ctrl_pending = 1U;
        s->play_lossless_payload_min = 0xFFFFU;
        uacm_session_audio_reset(s);
    }
}

static void uacm_build_header(audio_header_t *h, uacm_session_t *s,
                              uint8_t packet_type, uint8_t direction,
                              uint16_t data_len)
{
    h->magic = AUDIO_PACKET_MAGIC;
    h->seq_num = s->tx_seq++;
    h->timestamp = uacm_time_us();
    h->packet_type = packet_type;
    h->direction = direction;
    h->client_id = s->client_id;
    h->data_len = data_len;
}

static int uacm_open_listen(uint16_t port, const char *name)
{
    struct sockaddr_in addr;
    int fd;
    int reuse = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        dbg("UACM AP listen socket %s failed errno=%d\n", name, errno);
        return -1;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     (const char *)&reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        dbg("UACM AP bind %s port=%u errno=%d\n", name, (unsigned)port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        dbg("UACM AP listen %s errno=%d\n", name, errno);
        close(fd);
        return -1;
    }
    return fd;
}

static int uacm_recv_exact_timeout(int fd, uint8_t *dst, uint32_t len,
                                   uint32_t timeout_ms)
{
    uint32_t off = 0U;
    uint32_t start = uacm_now_ms();

    while (off < len) {
        int n = recv(fd, dst + off, len - off, MSG_DONTWAIT);
        if (n > 0) {
            off += (uint32_t)n;
            continue;
        }
        if (n == 0) {
            return -1;
        }
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            return -1;
        }
        if ((uacm_now_ms() - start) >= timeout_ms) {
            return -1;
        }
        rtos_task_suspend(1U);
    }
    return 0;
}

static int uacm_accept_hello(int listen_fd, uint8_t expected_direction,
                             const char *name)
{
    fd_set rfds;
    struct timeval tv;
    int sel;
    int fd;
    int nodelay = 1;
    /* HOSTBUF2: keep TX/general socket buffering at 4 KB, but request a
     * smaller 2 KB receive buffer for STA->AP record sockets.  This makes
     * lwIP/TCP backpressure arrive earlier during full-duplex RX bursts. */
    int sndbuf = (int)UACM_SOCKET_BUF_BYTES;
    int rcvbuf = (expected_direction == AUDIO_DIR_STA_TO_AP) ?
                 (int)UACM_RECORD_SOCKET_BUF_BYTES :
                 (int)UACM_SOCKET_BUF_BYTES;
    audio_header_t h;
    audio_ctrl_payload_t ctrl;
    uacm_session_t *s;
    uint8_t idx;

    if (listen_fd < 0) {
        return 0;
    }
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    sel = select(listen_fd + 1, &rfds, NULL, NULL, &tv);
    if ((sel <= 0) || !FD_ISSET(listen_fd, &rfds)) {
        return 0;
    }

    fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) {
        dbg("UACM_DBG [%s] accept FAILED listen_fd=%d errno=%d\n",
            name, listen_fd, errno);
        return -1;
    }
    dbg("UACM_DBG [%s] accept OK new_fd=%d (waiting HELLO)\n",
        name, fd);
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     (const char *)&nodelay, sizeof(nodelay));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                     (const char *)&sndbuf, sizeof(sndbuf));
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                     (const char *)&rcvbuf, sizeof(rcvbuf));

    {
        /* Structured stepwise validation so a failed recv never prints stack
         * garbage as if it were HELLO content.  fail_code pinpoints the exact
         * reason. */
        int hdr_rc = uacm_recv_exact_timeout(fd, (uint8_t *)&h, sizeof(h),
                                             UACM_HELLO_TIMEOUT_MS);
        uint8_t fail_code = 0U; /* 0=ok, 1=hdr_recv, 2=magic, 3=type,
                                   4=dir, 5=dlen, 6=ctrl_recv,
                                   7=ctrl_type, 8=id_low, 9=id_high,
                                   10=ctrl_val */
        if (hdr_rc != 0) {
            fail_code = 1U;
        } else if (h.magic != AUDIO_PACKET_MAGIC) {
            fail_code = 2U;
        } else if (h.packet_type != AUDIO_PACKET_TYPE_CTRL) {
            fail_code = 3U;
        } else if (h.direction != expected_direction) {
            fail_code = 4U;
        } else if (h.data_len != sizeof(ctrl)) {
            fail_code = 5U;
        } else if (uacm_recv_exact_timeout(fd, (uint8_t *)&ctrl, sizeof(ctrl),
                                           UACM_HELLO_TIMEOUT_MS) != 0) {
            fail_code = 6U;
        } else if (ctrl.ctrl_type != AUDIO_CTRL_SESSION_HELLO) {
            fail_code = 7U;
        } else if (h.client_id < UACM_FIRST_CLIENT_ID) {
            fail_code = 8U;
        } else if (h.client_id >= (UACM_FIRST_CLIENT_ID + UACM_SESSION_COUNT)) {
            fail_code = 9U;
        } else if (ctrl.value != h.client_id) {
            fail_code = 10U;
        }

        if (fail_code != 0U) {
            dbg("UACM AP reject %s hello fd=%d fail=%u\n",
                name, fd, (unsigned)fail_code);
            dbg("UACM_DBG reject detail: h.magic=0x%08x type=0x%02x dir=%u "
                "dlen=%u id=%u ctrl_type=0x%02x ctrl_val=%u\n",
                (unsigned)h.magic, (unsigned)h.packet_type,
                (unsigned)h.direction, (unsigned)h.data_len,
                (unsigned)h.client_id, (unsigned)ctrl.ctrl_type,
                (unsigned)ctrl.value);
            uacm_close_fd(&fd);
            return -1;
        }
    }

    idx = (uint8_t)(h.client_id - UACM_FIRST_CLIENT_ID);
    s = &s_session[idx];
    if (expected_direction == AUDIO_DIR_AP_TO_STA) {
        if (s->playback_fd >= 0) {
            s->reconnects++;
            uacm_close_playback(s);
        }
        s->playback_fd = fd;
        s->ctrl_pending = 1U;
        s->last_tx_ms = uacm_now_ms();
        if (!s->playback_seen) {
            dbg("UACM READY triangle%u playback connected\n",
                (unsigned)s->client_id);
        } else if (s->playback_down_reported) {
            dbg("UACM RECOVER triangle%u playback reconnected\n",
                (unsigned)s->client_id);
        }
        s->playback_seen = 1U;
        s->playback_down_reported = 0U;
    } else {
        if (s->record_fd >= 0) {
            s->reconnects++;
            uacm_close_record(s);
        }
        s->record_fd = fd;
        uacm_rx_lock(s);
        s->rx_len = 0U;
        s->rx_seq_valid = 0U;
        uacm_ring_reset(&s->rx_ring);
        s->rx_started = 0U;
        s->rx_conceal_blocks = 0U;
        s->rx_last_output_valid = 0U;
        s->rx_last_input_valid = 0U;
        s->rx_16k_prev_valid = 0U;
        s->rx_16k_prev[0] = s->rx_16k_prev[1] = 0;
        s->rx_drift_level_sum = 0U;
        s->rx_drift_block_count = 0U;
        uacm_rx_unlock(s);
        if (!s->record_seen) {
            dbg("UACM READY triangle%u record connected\n",
                (unsigned)s->client_id);
        } else if (s->record_down_reported) {
            dbg("UACM RECOVER triangle%u record reconnected\n",
                (unsigned)s->client_id);
        }
        s->record_seen = 1U;
        s->record_down_reported = 0U;
        if (s_usb_mic_on) {
            uint32_t now_ms = uacm_now_ms();
            /* R6: a TCP reconnect does not change the Wi-Fi association.
             * Reissuing disable_rx on every record reconnect caused repeated
             * BA negotiation and amplified the STA2 reconnect storm.  Allow
             * a refresh only after a long guard interval, which still covers
             * a genuine later Wi-Fi reassociation. */
            if ((s_ap_rx_agg_last_apply_ms == 0U) ||
                ((now_ms - s_ap_rx_agg_last_apply_ms) >=
                 UACM_AGG_REAPPLY_GUARD_MS)) {
                s_ap_rx_agg_update_pending = 1U;
            }
        }
    }
    return 1;
}

static void uacm_enqueue_playback(uint8_t full_duplex)
{
    uint8_t pcm[UACM_PLAYBACK_PCM_BYTES] __attribute__((aligned(4)));
    /* P10: aggregate two consecutive 5 ms USB-side blocks into one 10 ms
     * AP->STA TCP application packet.  The full-duplex enqueue budget is
     * halved in block-count terms so the maximum audio time drained per
     * scheduler pass does not increase. */
    uint32_t budget = full_duplex ? UACM_PLAY_ENQUEUE_FD_BUDGET :
                                    UACM_PLAY_ENQUEUE_IDLE_BUDGET;
    uint32_t i;

    while ((budget-- > 0U) &&
           (uacm_ring_used(&s_usb_spk_ring) >= UACM_PLAYBACK_PCM_BYTES)) {
        if (uacm_ring_read(&s_usb_spk_ring, pcm, sizeof(pcm)) != sizeof(pcm)) {
            break;
        }
        for (i = 0U; i < UACM_SESSION_COUNT; i++) {
            uacm_session_t *sess = &s_session[i];
            if (sess->playback_fd >= 0) {
                uacm_ring_keep_latest(&sess->tx_ring, pcm, sizeof(pcm),
                                      UACM_PLAYBACK_PCM_BYTES, &sess->playback_drop);
            }
        }
    }
}

static void uacm_prepare_ctrl(uacm_session_t *s, uint8_t type, uint8_t value)
{
    audio_header_t h;
    audio_ctrl_payload_t ctrl;

    ctrl.ctrl_type = type;
    ctrl.value = value;
    ctrl.reserved = 0U;
    uacm_build_header(&h, s, AUDIO_PACKET_TYPE_CTRL,
                      AUDIO_DIR_AP_TO_STA, sizeof(ctrl));
    memcpy(s->tx_wire, &h, sizeof(h));
    memcpy(s->tx_wire + sizeof(h), &ctrl, sizeof(ctrl));
    s->tx_len = (uint16_t)(sizeof(h) + sizeof(ctrl));
    s->tx_off = 0U;
    s->tx_kind = UACM_TX_KIND_CTRL;
    s->tx_lossless = 0U;
    s->tx_payload_len = 0U;
    s->tx_progress_ms = uacm_now_ms();
}

static int uacm_lossless_encode_payload(const int16_t *pcm, uint16_t frames,
                                        uint16_t raw_len, uint8_t *out,
                                        uint16_t out_cap, uint16_t *out_len);

static void uacm_fill_pcm_wire(uacm_session_t *s, uint8_t packet_type,
                               const uint8_t *payload, uint16_t payload_len,
                               uint8_t lossless)
{
    audio_header_t h;
    uacm_build_header(&h, s, packet_type, AUDIO_DIR_AP_TO_STA, payload_len);
    memcpy(s->tx_wire, &h, sizeof(h));
    if (payload != (s->tx_wire + sizeof(h)))
        memcpy(s->tx_wire + sizeof(h), payload, payload_len);
    s->tx_len = (uint16_t)(sizeof(h) + payload_len);
    s->tx_off = 0U;
    s->tx_kind = UACM_TX_KIND_PCM;
    s->tx_lossless = lossless;
    s->tx_payload_len = payload_len;
    s->tx_progress_ms = uacm_now_ms();
}

static int uacm_prepare_pcm(uacm_session_t *s)
{
    int16_t raw[UACM_PLAYBACK_PCM_BYTES / sizeof(int16_t)] __attribute__((aligned(4)));
    uint8_t *payload = s->tx_wire + sizeof(audio_header_t);
    uint16_t payload_len = UACM_PLAYBACK_PCM_BYTES;
    uint8_t packet_type = AUDIO_PACKET_TYPE_UAC_PCM;
    uint8_t lossless = 0U;
    uint64_t enc_start, enc_us;
    int comp_ret = 1;

    if (uacm_ring_used(&s->tx_ring) < UACM_PLAYBACK_PCM_BYTES) return -1;
    if (uacm_ring_read(&s->tx_ring, (uint8_t *)raw, UACM_PLAYBACK_PCM_BYTES) !=
        UACM_PLAYBACK_PCM_BYTES) return -1;

    {
        s_play_lossless_encode_calls++;
        enc_start = uacm_time_us();
        comp_ret = uacm_lossless_encode_payload(raw, UACM_PLAYBACK_FRAMES_10MS,
                                                UACM_PLAYBACK_PCM_BYTES, payload,
                                                UACM_PLAYBACK_PCM_BYTES, &payload_len);
        enc_us = uacm_time_us() - enc_start;
        if (enc_us > s->play_lossless_encode_max_us)
            s->play_lossless_encode_max_us = (uint32_t)enc_us;
        if (enc_us > UACM_LOSSLESS_PLAY_WARN_US)
            s_play_lossless_slow_hits++;
    }
    if (comp_ret == 0) {
        packet_type = AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS;
        lossless = 1U;
    } else {
        payload_len = UACM_PLAYBACK_PCM_BYTES;
        memcpy(payload, raw, payload_len);
    }
    uacm_fill_pcm_wire(s, packet_type, payload, payload_len, lossless);
    return 0;
}

/* Common case: both STA playback queues contain the same 10 ms USB block.
 * Encode that block once, copy only the coded payload, and build an independent
 * TCP/application header for each STA.  If queues diverge, leave them untouched
 * and fall back to the per-session path above. */
static int uacm_prepare_pcm_pair(void)
{
    uacm_session_t *a = &s_session[0];
    uacm_session_t *b = &s_session[1];
    int16_t raw[UACM_PLAYBACK_PCM_BYTES / sizeof(int16_t)] __attribute__((aligned(4)));
    uint8_t *payload_a = a->tx_wire + sizeof(audio_header_t);
    uint8_t *payload_b = b->tx_wire + sizeof(audio_header_t);
    uint16_t payload_len = UACM_PLAYBACK_PCM_BYTES;
    uint8_t packet_type = AUDIO_PACKET_TYPE_UAC_PCM;
    uint8_t lossless = 0U;
    uint64_t enc_start, enc_us;
    int comp_ret = 1;

    if ((a->playback_fd < 0) || (b->playback_fd < 0) ||
        (a->tx_len != 0U) || (b->tx_len != 0U) ||
        a->ctrl_pending || b->ctrl_pending ||
        (uacm_ring_used(&a->tx_ring) < UACM_PLAYBACK_PCM_BYTES) ||
        (uacm_ring_used(&b->tx_ring) < UACM_PLAYBACK_PCM_BYTES)) return -1;
    if (uacm_ring_peek_copy(&a->tx_ring, (uint8_t *)raw,
                            UACM_PLAYBACK_PCM_BYTES) != UACM_PLAYBACK_PCM_BYTES) return -1;
    if (!uacm_ring_peek_equal(&b->tx_ring, (const uint8_t *)raw,
                              UACM_PLAYBACK_PCM_BYTES)) return -1;
    (void)uacm_ring_read(&a->tx_ring, NULL, UACM_PLAYBACK_PCM_BYTES);
    (void)uacm_ring_read(&b->tx_ring, NULL, UACM_PLAYBACK_PCM_BYTES);

    {
        s_play_lossless_encode_calls++;
        enc_start = uacm_time_us();
        comp_ret = uacm_lossless_encode_payload(raw, UACM_PLAYBACK_FRAMES_10MS,
                                                UACM_PLAYBACK_PCM_BYTES, payload_a,
                                                UACM_PLAYBACK_PCM_BYTES, &payload_len);
        enc_us = uacm_time_us() - enc_start;
        if (enc_us > a->play_lossless_encode_max_us)
            a->play_lossless_encode_max_us = (uint32_t)enc_us;
        if (enc_us > b->play_lossless_encode_max_us)
            b->play_lossless_encode_max_us = (uint32_t)enc_us;
        if (enc_us > UACM_LOSSLESS_PLAY_WARN_US)
            s_play_lossless_slow_hits++;
    }
    if (comp_ret == 0) {
        packet_type = AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS;
        lossless = 1U;
    } else {
        payload_len = UACM_PLAYBACK_PCM_BYTES;
        memcpy(payload_a, raw, payload_len);
    }
    memcpy(payload_b, payload_a, payload_len);
    uacm_fill_pcm_wire(a, packet_type, payload_a, payload_len, lossless);
    uacm_fill_pcm_wire(b, packet_type, payload_b, payload_len, lossless);
    s_play_lossless_shared_frames++;
    return 0;
}

static int uacm_service_tx(uacm_session_t *s, uint32_t now_ms)
{
    int n;

    if (s->playback_fd < 0) {
        return -1;
    }
    //
    if (s->tx_len == 0U) {
        if (s->ctrl_pending) {
            uint8_t mic_on = s->ctrl_mic_on;
            s->ctrl_pending = 0U;
            uacm_prepare_ctrl(s, AUDIO_CTRL_UAC_MIC_STREAMING, mic_on);
        } else if (uacm_prepare_pcm(s) == 0) {
            /* ready */
        } else if ((now_ms - s->last_tx_ms) >= UACM_HEARTBEAT_MS) {
            uacm_prepare_ctrl(s, AUDIO_CTRL_HEARTBEAT, s_usb_mic_on);
        } else {
            return 0;
        }
    }

    n = send(s->playback_fd, s->tx_wire + s->tx_off,
             s->tx_len - s->tx_off, MSG_DONTWAIT);
    if (n > 0) {
        s->tx_blocked_since_ms = 0U;
        s->tx_last_errno = 0U;
        s->tx_off = (uint16_t)(s->tx_off + (uint16_t)n);
        s->tx_progress_ms = now_ms;
        if (s->tx_off >= s->tx_len) {
            if (s->tx_kind == UACM_TX_KIND_PCM) {
                s->playback_packets++;
                if (s->tx_lossless) s->play_lossless_comp_packets++;
                else s->play_lossless_raw_packets++;
                s->play_lossless_payload_bytes += s->tx_payload_len;
                if (s->tx_payload_len < s->play_lossless_payload_min)
                    s->play_lossless_payload_min = s->tx_payload_len;
                if (s->tx_payload_len > s->play_lossless_payload_max)
                    s->play_lossless_payload_max = s->tx_payload_len;
            }
            s->tx_len = 0U;
            s->tx_off = 0U;
            s->tx_kind = UACM_TX_KIND_NONE;
            s->tx_lossless = 0U;
            s->tx_payload_len = 0U;
            s->last_tx_ms = now_ms;
        }
        return 0;
    }
    if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) ||
                      (errno == ENOMEM)
#ifdef ENOBUFS
                      || (errno == ENOBUFS)
#endif
                      )) {
        /* lwIP can report transient ENOMEM/ENOBUFS when its pbuf/TCP queues
         * are momentarily full.  Treat that like backpressure, not a broken
         * TCP connection. */
        /* Preserve a complete PCM frame during transient backpressure.  The
         * former 50 ms stale-frame drop converted a recoverable delay into a
         * guaranteed audible hole. */
        if (s->tx_blocked_since_ms == 0U) {
            s->tx_blocked_since_ms = now_ms;
        }
        s->tx_last_errno = (uint32_t)errno;
        s->tx_backpressure_count++;
        if ((now_ms - s->tx_blocked_since_ms) >=
            UACM_TX_BACKPRESSURE_TIMEOUT_MS) {
            dbg("UACM TX stalled triangle%u fd=%d errno=%u off=%u len=%u "
                "blocked=%ums retries=%u\n",
                (unsigned)s->client_id,
                s->playback_fd,
                (unsigned)s->tx_last_errno,
                (unsigned)s->tx_off,
                (unsigned)s->tx_len,
                (unsigned)(now_ms - s->tx_blocked_since_ms),
                (unsigned)s->tx_backpressure_count);
            return -1;
        }
        return 0;
    }
    return -1;
}

static uint32_t uacm_track_record_seq(uacm_session_t *s, uint32_t seq)
{
    uint32_t expect;

    if (!s->rx_seq_valid) {
        s->rx_seq_valid = 1U;
        s->rx_last_seq = seq;
        return 0U;
    }

    expect = s->rx_last_seq + 1U;
    if (seq == expect) {
        s->rx_last_seq = seq;
        return 0U;
    }
    if (seq > expect) {
        uint32_t gap = seq - expect;
        s->seq_gap += gap;
        s->rx_last_seq = seq;
        return gap;
    }

    s->seq_old++;
    return 0xFFFFFFFFU;
}

static void uacm_record_reset_jitter(uacm_session_t *s)
{
    uacm_ring_reset(&s->rx_ring);
    s->rx_started = 0U;
    s->rx_conceal_blocks = 0U;
    s->rx_last_output_valid = 0U;
    s->rx_last_input_valid = 0U;
    s->rx_16k_prev_valid = 0U;
    s->rx_drift_level_sum = 0U;
    s->rx_drift_block_count = 0U;
    s->rx_rebuffer_count++;
}

static void uacm_record_trim_delay(uacm_session_t *s)
{
    uint32_t used = uacm_ring_used(&s->rx_ring);

    if (used > UACM_RX_MAX_DELAY_BYTES) {
        uint32_t drop = used - UACM_RX_TRIM_KEEP_BYTES;
        drop -= drop % UAC_BRIDGE_FRAME_BYTES;
        if (drop != 0U) {
            (void)uacm_ring_read(&s->rx_ring, NULL, drop);
            s->record_drop += drop / UACM_PCM_5MS_BYTES;
            s->rx_drift_level_sum = 0U;
            s->rx_drift_block_count = 0U;
        }
    }
}

static void uacm_record_write_gap_bridge(uacm_session_t *s,
                                         const uint8_t *next_payload,
                                         uint32_t missing_packets)
{
    int16_t plc[UACM_SAMPLES_5MS] __attribute__((aligned(4)));
    int16_t next_l;
    int16_t next_r;
    uint32_t total_frames;
    uint32_t packet;

    if (!s->rx_last_input_valid || (next_payload == NULL) ||
        (missing_packets == 0U) ||
        (missing_packets > UACM_RX_PLC_MAX_LOST_PACKETS)) {
        return;
    }

    memcpy(&next_l, next_payload, sizeof(next_l));
    memcpy(&next_r, next_payload + sizeof(int16_t), sizeof(next_r));
    total_frames = missing_packets * UACM_FRAMES_5MS;

    for (packet = 0U; packet < missing_packets; packet++) {
        uint32_t frame;
        for (frame = 0U; frame < UACM_FRAMES_5MS; frame++) {
            uint32_t pos = packet * UACM_FRAMES_5MS + frame + 1U;
            int32_t l = (int32_t)s->rx_last_input[0] +
                (((int32_t)next_l - (int32_t)s->rx_last_input[0]) * (int32_t)pos) /
                (int32_t)(total_frames + 1U);
            int32_t r = (int32_t)s->rx_last_input[1] +
                (((int32_t)next_r - (int32_t)s->rx_last_input[1]) * (int32_t)pos) /
                (int32_t)(total_frames + 1U);
            plc[frame * UAC_BRIDGE_CHANNELS] = (int16_t)l;
            plc[frame * UAC_BRIDGE_CHANNELS + 1U] = (int16_t)r;
        }
        uacm_ring_keep_latest(&s->rx_ring, (const uint8_t *)plc,
                              UACM_PCM_5MS_BYTES, UACM_PCM_5MS_BYTES,
                              &s->record_drop);
    }
    s->rx_plc_frames += total_frames;
}

#if UACM_RECORD_SAMPLE_RATE_HZ == 16000U
static void uacm_upsample_record_16k_5ms_to_48k(uacm_session_t *s,
                                                  const uint8_t *src,
                                                  int16_t *dst)
{
    uint32_t in_frame;

    for (in_frame = 0U; in_frame < UACM_RECORD_FRAMES_5MS; in_frame++) {
        int16_t cur[UAC_BRIDGE_CHANNELS];
        uint32_t ch;
        uint32_t src_off = in_frame * UAC_BRIDGE_FRAME_BYTES;
        uint32_t out_frame = in_frame * 3U;

        memcpy(&cur[0], src + src_off, sizeof(int16_t));
        memcpy(&cur[1], src + src_off + sizeof(int16_t), sizeof(int16_t));
        if (!s->rx_16k_prev_valid) {
            s->rx_16k_prev[0] = cur[0];
            s->rx_16k_prev[1] = cur[1];
            s->rx_16k_prev_valid = 1U;
        }
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            int32_t prev = s->rx_16k_prev[ch];
            int32_t now = cur[ch];
            dst[(out_frame + 0U) * UAC_BRIDGE_CHANNELS + ch] =
                (int16_t)((2 * prev + now) / 3);
            dst[(out_frame + 1U) * UAC_BRIDGE_CHANNELS + ch] =
                (int16_t)((prev + 2 * now) / 3);
            dst[(out_frame + 2U) * UAC_BRIDGE_CHANNELS + ch] = cur[ch];
            s->rx_16k_prev[ch] = cur[ch];
        }
    }
}
#endif


/* ================= LL3-BPK2/48PREP1 bounded block lossless codec =================
 * Real-time oriented replacement for FAST1 whole-frame Rice coding.
 *
 * Wire v4 is frame-independent and bit-exact, but divides each channel into
 * 64-frame blocks.  Each channel block uses either:
 *   - DELTA1 + fixed bit-width packing, or
 *   - local RAW PCM when packing would not save bytes.
 *
 * There is no unary/Rice loop and no 200-frame RAW guard.  Runtime per block is
 * bounded by a small fixed number of sample operations.  A difficult/noisy
 * block falls back locally instead of forcing the whole 10 ms stream RAW.
 * The complete frame still falls back to legacy RAW if v4 would not be smaller
 * than the original PCM.  CRC32 is over the original interleaved PCM and is
 * verified after every compressed-frame decode.
 */
typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t pos;
    uint32_t acc;
    uint8_t bits;
    uint8_t overflow;
} uacm_bpk_bw_t;

typedef struct {
    const uint8_t *buf;
    uint32_t size;
    uint32_t pos;
    uint32_t acc;
    uint8_t bits;
} uacm_bpk_br_t;

static const uint32_t uacm_crc32_nib[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
    0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
    0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
};

static inline uint32_t uacm_crc32_byte(uint32_t crc, uint8_t v)
{
    crc ^= v;
    crc = (crc >> 4) ^ uacm_crc32_nib[crc & 0x0FU];
    crc = (crc >> 4) ^ uacm_crc32_nib[crc & 0x0FU];
    return crc;
}

static uint32_t uacm_lossless_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    /* Keep the tiny table because AP DRAM is tight, but remove most loop
     * branches by processing four bytes per iteration. */
    while (len >= 4U) {
        crc = uacm_crc32_byte(crc, data[0]);
        crc = uacm_crc32_byte(crc, data[1]);
        crc = uacm_crc32_byte(crc, data[2]);
        crc = uacm_crc32_byte(crc, data[3]);
        data += 4U;
        len -= 4U;
    }
    while (len-- != 0U) crc = uacm_crc32_byte(crc, *data++);
    return ~crc;
}

static uint32_t uacm_lossless_zigzag(int32_t x)
{
    return (x >= 0) ? ((uint32_t)x << 1) : (((uint32_t)(-x) << 1) - 1U);
}

static int32_t uacm_lossless_unzigzag(uint32_t u)
{
    return (u & 1U) ? -(int32_t)((u + 1U) >> 1) : (int32_t)(u >> 1);
}

static uint8_t uacm_bpk_width(uint32_t v)
{
    /* GCC emits ARM CLZ: bounded cost, independent of signal amplitude. */
    return (v == 0U) ? 0U : (uint8_t)(32U - (uint32_t)__builtin_clz(v));
}

static void uacm_bpk_put_le16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)(u & 0xFFU);
    p[1] = (uint8_t)(u >> 8);
}

static int16_t uacm_bpk_get_le16(const uint8_t *p)
{
    return (int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void uacm_bpk_bw_init(uacm_bpk_bw_t *bw, uint8_t *buf, uint32_t cap)
{
    bw->buf = buf;
    bw->cap = cap;
    bw->pos = 0U;
    bw->acc = 0U;
    bw->bits = 0U;
    bw->overflow = 0U;
}

static inline void uacm_bpk_bw_put(uacm_bpk_bw_t *bw, uint32_t v, uint8_t width)
{
    if ((width == 0U) || bw->overflow) return;
    /* width came from this block's maximum; every v already fits. */
    bw->acc |= v << bw->bits;
    bw->bits = (uint8_t)(bw->bits + width);
    while (bw->bits >= 8U) {
        if (bw->pos >= bw->cap) {
            bw->overflow = 1U;
            return;
        }
        bw->buf[bw->pos++] = (uint8_t)(bw->acc & 0xFFU);
        bw->acc >>= 8;
        bw->bits = (uint8_t)(bw->bits - 8U);
    }
}

static int uacm_bpk_bw_finish(uacm_bpk_bw_t *bw, uint32_t expected)
{
    if (bw->overflow) return -1;
    if (bw->bits != 0U) {
        if (bw->pos >= bw->cap) return -1;
        bw->buf[bw->pos++] = (uint8_t)(bw->acc & 0xFFU);
        bw->acc = 0U;
        bw->bits = 0U;
    }
    return (bw->pos == expected) ? 0 : -1;
}

static void uacm_bpk_br_init(uacm_bpk_br_t *br, const uint8_t *buf, uint32_t size)
{
    br->buf = buf;
    br->size = size;
    br->pos = 0U;
    br->acc = 0U;
    br->bits = 0U;
}

static inline int uacm_bpk_br_get(uacm_bpk_br_t *br, uint8_t width,
                                   uint32_t mask, uint32_t *v)
{
    if (width == 0U) {
        *v = 0U;
        return 0;
    }
    while (br->bits < width) {
        if (br->pos >= br->size) return -1;
        br->acc |= (uint32_t)br->buf[br->pos++] << br->bits;
        br->bits = (uint8_t)(br->bits + 8U);
    }
    *v = br->acc & mask;
    br->acc >>= width;
    br->bits = (uint8_t)(br->bits - width);
    return 0;
}

static int uacm_bpk_encode_subblock(const int16_t *pcm, uint16_t start,
                                    uint16_t count, uint32_t ch,
                                    uint8_t *out, uint32_t cap,
                                    uint16_t *used, uint8_t *raw_used)
{
    uint32_t max_u = 0U;
    uint32_t i;
    uint8_t width;
    uint32_t packed_bytes;
    uint32_t comp_bytes;
    uint32_t raw_bytes;

    if ((count == 0U) || (cap == 0U)) return -1;
    for (i = 1U; i < count; i++) {
        int32_t cur = pcm[((uint32_t)start + i) * UAC_BRIDGE_CHANNELS + ch];
        int32_t prev = pcm[((uint32_t)start + i - 1U) * UAC_BRIDGE_CHANNELS + ch];
        uint32_t u = uacm_lossless_zigzag(cur - prev);
        if (u > max_u) max_u = u;
    }
    width = uacm_bpk_width(max_u);
    packed_bytes = (((uint32_t)count - 1U) * width + 7U) >> 3;
    comp_bytes = 1U + 2U + packed_bytes; /* descriptor + warm sample + bits */
    raw_bytes = 1U + (uint32_t)count * 2U;

    if ((width > UACM_LOSSLESS_MAX_WIDTH) || (comp_bytes >= raw_bytes)) {
        if (cap < raw_bytes) return -1;
        out[0] = UACM_LOSSLESS_DESC_RAW;
        for (i = 0U; i < count; i++) {
            uacm_bpk_put_le16(out + 1U + i * 2U,
                pcm[((uint32_t)start + i) * UAC_BRIDGE_CHANNELS + ch]);
        }
        *used = (uint16_t)raw_bytes;
        *raw_used = 1U;
        return 0;
    } else {
        uacm_bpk_bw_t bw;
        int16_t prev;
        if (cap < comp_bytes) return -1;
        out[0] = width;
        prev = pcm[(uint32_t)start * UAC_BRIDGE_CHANNELS + ch];
        uacm_bpk_put_le16(out + 1U, prev);
        uacm_bpk_bw_init(&bw, out + 3U, packed_bytes);
        for (i = 1U; i < count; i++) {
            int16_t cur = pcm[((uint32_t)start + i) * UAC_BRIDGE_CHANNELS + ch];
            uint32_t u = uacm_lossless_zigzag((int32_t)cur - (int32_t)prev);
            uacm_bpk_bw_put(&bw, u, width);
            prev = cur;
        }
        if (uacm_bpk_bw_finish(&bw, packed_bytes) != 0) return -1;
        *used = (uint16_t)comp_bytes;
        *raw_used = 0U;
        return 0;
    }
}

static int uacm_bpk_decode_subblock(const uint8_t *in, uint32_t avail,
                                    uint16_t start, uint16_t count,
                                    uint32_t ch, int16_t *pcm,
                                    uint16_t *used)
{
    uint8_t desc;
    uint32_t i;
    if ((count == 0U) || (avail == 0U)) return -1;
    desc = in[0];
    if (desc == UACM_LOSSLESS_DESC_RAW) {
        uint32_t bytes = 1U + (uint32_t)count * 2U;
        if (avail < bytes) return -1;
        for (i = 0U; i < count; i++) {
            pcm[((uint32_t)start + i) * UAC_BRIDGE_CHANNELS + ch] =
                uacm_bpk_get_le16(in + 1U + i * 2U);
        }
        *used = (uint16_t)bytes;
        return 0;
    } else {
        uint8_t width = desc & UACM_LOSSLESS_DESC_WIDTH_MASK;
        uint32_t packed_bytes;
        uint32_t bytes;
        uint32_t mask;
        int16_t prev;
        uacm_bpk_br_t br;
        if ((desc & 0x60U) != 0U || width > UACM_LOSSLESS_MAX_WIDTH) return -1;
        mask = (width == 0U) ? 0U : ((1U << width) - 1U);
        packed_bytes = (((uint32_t)count - 1U) * width + 7U) >> 3;
        bytes = 3U + packed_bytes;
        if (avail < bytes) return -1;
        prev = uacm_bpk_get_le16(in + 1U);
        pcm[(uint32_t)start * UAC_BRIDGE_CHANNELS + ch] = prev;
        uacm_bpk_br_init(&br, in + 3U, packed_bytes);
        for (i = 1U; i < count; i++) {
            uint32_t u;
            int32_t sample;
            if (uacm_bpk_br_get(&br, width, mask, &u) != 0) return -1;
            sample = (int32_t)prev + uacm_lossless_unzigzag(u);
            if ((sample < -32768) || (sample > 32767)) return -1;
            prev = (int16_t)sample;
            pcm[((uint32_t)start + i) * UAC_BRIDGE_CHANNELS + ch] = prev;
        }
        *used = (uint16_t)bytes;
        return 0;
    }
}

static int uacm_lossless_encode_payload(const int16_t *pcm, uint16_t frames,
                                        uint16_t raw_len, uint8_t *out,
                                        uint16_t out_cap, uint16_t *out_len)
{
    uacm_lossless_header_t lh;
    uint32_t pos = sizeof(lh);
    uint16_t start = 0U;
    uint8_t block_count;
    uint8_t b;

    if ((pcm == NULL) || (out == NULL) || (out_len == NULL) ||
        ((uint32_t)frames * UAC_BRIDGE_CHANNELS * sizeof(int16_t) != raw_len) ||
        (out_cap <= sizeof(lh))) return 1;
    block_count = (uint8_t)(((uint32_t)frames + UACM_LOSSLESS_BLOCK_FRAMES - 1U) /
                            UACM_LOSSLESS_BLOCK_FRAMES);
    lh.magic = UACM_LOSSLESS_MAGIC;
    lh.version = UACM_LOSSLESS_VERSION;
    lh.block_frames = UACM_LOSSLESS_BLOCK_FRAMES;
    lh.raw_len = raw_len;
    lh.block_count = block_count;
    lh.reserved = 0U;
    lh.raw_crc32 = 0U;

    for (b = 0U; b < block_count; b++) {
        uint16_t count = (uint16_t)(frames - start);
        uint32_t ch;
        if (count > UACM_LOSSLESS_BLOCK_FRAMES) count = UACM_LOSSLESS_BLOCK_FRAMES;
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            uint16_t used = 0U;
            uint8_t local_raw = 0U;
            if (uacm_bpk_encode_subblock(pcm, start, count, ch, out + pos,
                                         (uint32_t)out_cap - pos,
                                         &used, &local_raw) != 0) return 1;
            (void)local_raw;
            pos += used;
            /* Abort early: whole-frame RAW is both smaller and cheaper. */
            if (pos >= raw_len) return 1;
        }
        start = (uint16_t)(start + count);
    }
    if ((start != frames) || (pos >= raw_len) || (pos > out_cap)) return 1;
    lh.raw_crc32 = uacm_lossless_crc32((const uint8_t *)pcm, raw_len);
    memcpy(out, &lh, sizeof(lh));
    *out_len = (uint16_t)pos;
    return 0;
}

/* return 0=bit-exact success, -1=format/decode failure, -2=CRC mismatch,
 *       -3=decoded-length/header mismatch */
static int uacm_lossless_decode_payload(const uint8_t *payload,
                                        uint16_t payload_len,
                                        uint16_t expected_raw_len,
                                        uint16_t frames, int16_t *pcm_out)
{
    uacm_lossless_header_t lh;
    uint32_t pos = sizeof(lh);
    uint16_t start = 0U;
    uint8_t expected_blocks;
    uint8_t b;
    uint32_t crc;
    if ((payload == NULL) || (pcm_out == NULL) ||
        (payload_len < sizeof(lh)) ||
        ((uint32_t)frames * UAC_BRIDGE_CHANNELS * sizeof(int16_t) != expected_raw_len)) return -3;
    memcpy(&lh, payload, sizeof(lh));
    expected_blocks = (uint8_t)(((uint32_t)frames + UACM_LOSSLESS_BLOCK_FRAMES - 1U) /
                                UACM_LOSSLESS_BLOCK_FRAMES);
    if ((lh.magic != UACM_LOSSLESS_MAGIC) ||
        (lh.version != UACM_LOSSLESS_VERSION) ||
        (lh.block_frames != UACM_LOSSLESS_BLOCK_FRAMES) ||
        (lh.raw_len != expected_raw_len) ||
        (lh.block_count != expected_blocks)) return -3;
    if (lh.reserved != 0U) return -1;

    for (b = 0U; b < lh.block_count; b++) {
        uint16_t count = (uint16_t)(frames - start);
        uint32_t ch;
        if (count > UACM_LOSSLESS_BLOCK_FRAMES) count = UACM_LOSSLESS_BLOCK_FRAMES;
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            uint16_t used = 0U;
            if (pos >= payload_len) return -1;
            if (uacm_bpk_decode_subblock(payload + pos, (uint32_t)payload_len - pos,
                                         start, count, ch, pcm_out, &used) != 0) return -1;
            pos += used;
        }
        start = (uint16_t)(start + count);
    }
    if ((start != frames) || (pos != payload_len)) return -1;
    crc = uacm_lossless_crc32((const uint8_t *)pcm_out, expected_raw_len);
    return (crc == lh.raw_crc32) ? 0 : -2;
}
/* ================= end LL3-BPK2/48PREP1 bounded block lossless codec ================= */

static void uacm_enqueue_record_5ms(uacm_session_t *s,
                                     const uint8_t *pcm_payload,
                                     uint32_t gap_blocks)
{
    int16_t last_l;
    int16_t last_r;
    uint32_t last_off = (UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_FRAME_BYTES;

    if (gap_blocks > UACM_RX_PLC_MAX_LOST_PACKETS) {
        /* One missing R18 packet is two 5 ms blocks and fits the PLC window.
         * For larger gaps, keep the mature policy: insert one short cross-fade
         * bridge and skip stale audio rather than forcing a long USB hole. */
        uacm_record_write_gap_bridge(s, pcm_payload, 1U);
    } else if (gap_blocks != 0U) {
        uacm_record_write_gap_bridge(s, pcm_payload, gap_blocks);
    }

    uacm_ring_keep_latest(&s->rx_ring, pcm_payload, UACM_PCM_5MS_BYTES,
                          UACM_PCM_5MS_BYTES, &s->record_drop);
    memcpy(&last_l, pcm_payload + last_off, sizeof(last_l));
    memcpy(&last_r, pcm_payload + last_off + sizeof(int16_t), sizeof(last_r));
    s->rx_last_input[0] = last_l;
    s->rx_last_input[1] = last_r;
    s->rx_last_input_valid = 1U;
    uacm_record_trim_delay(s);
    s->record_packets++; /* internal 5 ms block count */
}

static int uacm_decode_enqueue_record10(uacm_session_t *s,
                                         const uint8_t *payload,
                                         uint16_t payload_len,
                                         uint32_t gap_blocks,
                                         int16_t *decode_wire)
{
    uint64_t dec_start = uacm_time_us();
    uint64_t dec_us;
    int dec = uacm_lossless_decode_payload(payload, payload_len,
                                           UACM_RECORD_PCM_10MS_BYTES,
                                           UACM_RECORD_FRAMES_10MS,
                                           decode_wire);
    dec_us = uacm_time_us() - dec_start;
    if (dec_us > s->lossless_decode_max_us) s->lossless_decode_max_us = (uint32_t)dec_us;
    if (dec_us > UACM_LOSSLESS_RECORD_WARN_US) s->lossless_decode_slow_hits++;
    if (dec != 0) {
        if (dec == -2) s->lossless_crc_fail++;
        else if (dec == -3) s->lossless_len_fail++;
        else s->lossless_decode_fail++;
        return -1;
    }
    s->lossless_verify_ok++;
    uacm_enqueue_record_5ms(s, (const uint8_t *)decode_wire, gap_blocks);
    uacm_enqueue_record_5ms(s,
        (const uint8_t *)decode_wire + UACM_RECORD_PCM_5MS_BYTES, 0U);
    return 0;
}

static void uacm_handle_record_packet(uacm_session_t *s,
                                      const audio_header_t *h,
                                      const uint8_t *payload)
{
    uint32_t gap;
    int16_t lossless_decode_wire[UACM_RECORD_SAMPLES_10MS]
        __attribute__((aligned(4)));

    if ((h->direction != AUDIO_DIR_STA_TO_AP) ||
        (h->client_id != s->client_id)) return;

    if (h->packet_type == AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS20) {
        uacm_rec20_header_t gh;
        uint32_t gap_blocks;
        const uint8_t *p0;
        const uint8_t *p1;

        if (h->data_len < sizeof(gh)) { s->lossless_len_fail++; return; }
        memcpy(&gh, payload, sizeof(gh));
        if ((gh.magic != UACM_REC20_MAGIC) ||
            (gh.version != UACM_REC20_VERSION) ||
            (gh.subframes != UACM_RECORD_GROUP_SUBFRAMES) ||
            (gh.len0 == 0U) || (gh.len1 == 0U) ||
            ((uint32_t)sizeof(gh) + gh.len0 + gh.len1 != h->data_len)) {
            s->lossless_len_fail++;
            return;
        }
        p0 = payload + sizeof(gh);
        p1 = p0 + gh.len0;

        uacm_rx_lock(s);
        gap = uacm_track_record_seq(s, h->seq_num);
        if (gap == 0xFFFFFFFFU) { uacm_rx_unlock(s); return; }
        gap_blocks = gap * 4U; /* one missing normal REC20 group = 20 ms = four 5 ms blocks */

        if (uacm_decode_enqueue_record10(s, p0, gh.len0, gap_blocks,
                                         lossless_decode_wire) == 0) {
            /* Reuse the same 1920-byte decode scratch for the second half;
             * this is why REC20 does not increase AP static DRAM. */
            (void)uacm_decode_enqueue_record10(s, p1, gh.len1, 0U,
                                               lossless_decode_wire);
        }
        s->record_wire_packets++;
        s->record_group20_packets++;
        s->lossless_comp_packets++;
        s->lossless_payload_bytes += h->data_len;
        s->lossless_raw_equiv_bytes += UACM_RECORD_GROUP_RAW_BYTES;
        uacm_rx_unlock(s);
        return;
    }

    /* Safe compatibility/fallback path: one native-48k 10 ms packet. */
    if ((h->packet_type != AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS) &&
        (h->packet_type != AUDIO_PACKET_TYPE_UAC_PCM)) return;

    uacm_rx_lock(s);
    gap = uacm_track_record_seq(s, h->seq_num);
    if (gap == 0xFFFFFFFFU) { uacm_rx_unlock(s); return; }

    if (h->packet_type == AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS) {
        if (uacm_decode_enqueue_record10(s, payload, h->data_len, gap * 2U,
                                         lossless_decode_wire) != 0) {
            uacm_rx_unlock(s);
            return;
        }
        s->lossless_comp_packets++;
        s->lossless_payload_bytes += h->data_len;
    } else if (h->data_len == UACM_RECORD_PCM_10MS_BYTES) {
        uacm_enqueue_record_5ms(s, payload, gap * 2U);
        uacm_enqueue_record_5ms(s, payload + UACM_RECORD_PCM_5MS_BYTES, 0U);
        s->lossless_raw_packets++;
        s->lossless_payload_bytes += h->data_len;
    } else {
        s->lossless_len_fail++;
        uacm_rx_unlock(s);
        return;
    }
    s->record_wire_packets++;
    s->record_fallback10_packets++;
    s->lossless_raw_equiv_bytes += UACM_RECORD_PCM_10MS_BYTES;
    uacm_rx_unlock(s);
}

static void uacm_parse_record_stream(uacm_session_t *s)
{
    while (s->rx_len >= sizeof(audio_header_t)) {
        audio_header_t h;
        uint32_t packet_len;

        memcpy(&h, s->rx_stream, sizeof(h));
        if (h.magic != AUDIO_PACKET_MAGIC) {
            memmove(s->rx_stream, s->rx_stream + 1, s->rx_len - 1U);
            s->rx_len--;
            continue;
        }
        /* REC20 container is deliberately capped to the existing 1920-byte
         * payload budget, so DRAMFIX1 rx_stream size does not grow. */
        if (h.data_len > UACM_RECORD_PCM_10MS_BYTES) {
            s->record_drop++;
            s->rx_len = 0U;
            return;
        }
        packet_len = sizeof(h) + h.data_len;
        if (s->rx_len < packet_len) {
            return;
        }
        uacm_handle_record_packet(s, &h, s->rx_stream + sizeof(h));
        if (s->rx_len > packet_len) {
            memmove(s->rx_stream, s->rx_stream + packet_len,
                    s->rx_len - packet_len);
        }
        s->rx_len = (uint16_t)(s->rx_len - packet_len);
    }
}

static int uacm_service_rx(uacm_session_t *s, uint32_t read_budget)
{
    if (s->record_fd < 0) {
        return -1;
    }

    uacm_parse_record_stream(s);
    while (read_budget-- > 0U) {
        int n;
        uint32_t free_bytes = sizeof(s->rx_stream) - s->rx_len;

        if (free_bytes == 0U) {
            s->record_drop++;
            s->rx_len = 0U;
            free_bytes = sizeof(s->rx_stream);
        }

        n = recv(s->record_fd, s->rx_stream + s->rx_len,
                 free_bytes, MSG_DONTWAIT);
        if (n > 0) {
            s->rx_len = (uint16_t)(s->rx_len + (uint16_t)n);
            uacm_parse_record_stream(s);
            continue;
        }
        if (n == 0) {
            return -1;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK) ||
            (errno == ENOMEM)
#ifdef ENOBUFS
            || (errno == ENOBUFS)
#endif
            ) {
            return 0;
        }
        return -1;
    }
    return 0;
}

static void uacm_apply_record_rx_agg_mode(uint8_t disable)
{
#if PLF_WIFI_STACK && (PLF_AIC8800MC || PLF_AIC8800M40 || defined(CFG_WIFI_RAM_VER))
    uint8_t i;
    /* R11: always keep AP record RX AMPDU enabled.  TCP preserves byte order
     * and the AP sequence/PLC layer already handles application-level gaps.
     * Disabling aggregation at 100 normal REC20 record wire packets/s increases descriptor, ACK
     * and host-buffer pressure and is the common trigger for R10 TXSTALE bursts. */
    (void)disable;
    for (i = 0U; i < UACM_AP_AUDIO_STA_COUNT; i++) {
        (void)rwnx_set_disable_agg_req(0U, 0U, i);
    }
#else
    (void)disable;
#endif
}

static void uacm_log_lossless_snapshot(void)
{
    uint32_t play_total1 = s_session[0].play_lossless_comp_packets +
                           s_session[0].play_lossless_raw_packets;
    uint32_t play_total2 = s_session[1].play_lossless_comp_packets +
                           s_session[1].play_lossless_raw_packets;
    uint32_t play_avg1 = play_total1 ?
        (s_session[0].play_lossless_payload_bytes / play_total1) : 0U;
    uint32_t play_avg2 = play_total2 ?
        (s_session[1].play_lossless_payload_bytes / play_total2) : 0U;
    uint32_t play_ratio1 = play_total1 ?
        (uint32_t)(((uint64_t)s_session[0].play_lossless_payload_bytes * 1000ULL) /
                   ((uint64_t)play_total1 * UACM_PLAYBACK_PCM_BYTES)) : 0U;
    uint32_t play_ratio2 = play_total2 ?
        (uint32_t)(((uint64_t)s_session[1].play_lossless_payload_bytes * 1000ULL) /
                   ((uint64_t)play_total2 * UACM_PLAYBACK_PCM_BYTES)) : 0U;
    uint32_t rec_total1 = s_session[0].lossless_comp_packets +
                          s_session[0].lossless_raw_packets;
    uint32_t rec_total2 = s_session[1].lossless_comp_packets +
                          s_session[1].lossless_raw_packets;
    uint32_t rec_avg1 = rec_total1 ?
        (s_session[0].lossless_payload_bytes / rec_total1) : 0U;
    uint32_t rec_avg2 = rec_total2 ?
        (s_session[1].lossless_payload_bytes / rec_total2) : 0U;
    uint32_t rec_ratio1 = s_session[0].lossless_raw_equiv_bytes ?
        (uint32_t)(((uint64_t)s_session[0].lossless_payload_bytes * 1000ULL) /
                   (uint64_t)s_session[0].lossless_raw_equiv_bytes) : 0U;
    uint32_t rec_ratio2 = s_session[1].lossless_raw_equiv_bytes ?
        (uint32_t)(((uint64_t)s_session[1].lossless_payload_bytes * 1000ULL) /
                   (uint64_t)s_session[1].lossless_raw_equiv_bytes) : 0U;

    dbg("UACM PLAYLOSS-SAFE T1 comp/raw=%u/%u avg=%uB ratio=%u.%u%% min=%u max=%u enc_wall_max=%uus | T2 comp/raw=%u/%u avg=%uB ratio=%u.%u%% min=%u max=%u enc_wall_max=%uus shared=%u cpu_est_max=%uus probe=%u pfail=%u slow_wall(>%uus)=%u\n",
        (unsigned)s_session[0].play_lossless_comp_packets,
        (unsigned)s_session[0].play_lossless_raw_packets,
        (unsigned)play_avg1, (unsigned)(play_ratio1 / 10U), (unsigned)(play_ratio1 % 10U),
        (unsigned)((s_session[0].play_lossless_payload_min == 0xFFFFU) ? 0U : s_session[0].play_lossless_payload_min),
        (unsigned)s_session[0].play_lossless_payload_max,
        (unsigned)s_session[0].play_lossless_encode_max_us,
        (unsigned)s_session[1].play_lossless_comp_packets,
        (unsigned)s_session[1].play_lossless_raw_packets,
        (unsigned)play_avg2, (unsigned)(play_ratio2 / 10U), (unsigned)(play_ratio2 % 10U),
        (unsigned)((s_session[1].play_lossless_payload_min == 0xFFFFU) ? 0U : s_session[1].play_lossless_payload_min),
        (unsigned)s_session[1].play_lossless_payload_max,
        (unsigned)s_session[1].play_lossless_encode_max_us,
        (unsigned)s_play_lossless_shared_frames,
        (unsigned)s_play_lossless_probe_est_max_us,
        (unsigned)s_play_lossless_probe_count,
        (unsigned)s_play_lossless_probe_fail,
        (unsigned)UACM_LOSSLESS_PLAY_WARN_US,
        (unsigned)s_play_lossless_slow_hits);

    dbg("UACM RECLOSS-SAFE T1 comp/raw=%u/%u avg=%uB ratio=%u.%u%% raw/dec=%u/%u match=%u crc/dec/len_fail=%u/%u/%u dec_wall_max=%uus cpu_est_max=%uus probe=%u pfail=%u slow_wall(>%uus)=%u | T2 comp/raw=%u/%u avg=%uB ratio=%u.%u%% raw/dec=%u/%u match=%u crc/dec/len_fail=%u/%u/%u dec_wall_max=%uus cpu_est_max=%uus probe=%u pfail=%u slow_wall(>%uus)=%u\n",
        (unsigned)s_session[0].lossless_comp_packets,
        (unsigned)s_session[0].lossless_raw_packets,
        (unsigned)rec_avg1, (unsigned)(rec_ratio1 / 10U), (unsigned)(rec_ratio1 % 10U),
        (unsigned)UACM_RECORD_PCM_10MS_BYTES, (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)s_session[0].lossless_verify_ok,
        (unsigned)s_session[0].lossless_crc_fail,
        (unsigned)s_session[0].lossless_decode_fail,
        (unsigned)s_session[0].lossless_len_fail,
        (unsigned)s_session[0].lossless_decode_max_us,
        (unsigned)s_session[0].lossless_decode_probe_est_max_us,
        (unsigned)s_session[0].lossless_decode_probe_count,
        (unsigned)s_session[0].lossless_decode_probe_fail,
        (unsigned)UACM_LOSSLESS_RECORD_WARN_US,
        (unsigned)s_session[0].lossless_decode_slow_hits,
        (unsigned)s_session[1].lossless_comp_packets,
        (unsigned)s_session[1].lossless_raw_packets,
        (unsigned)rec_avg2, (unsigned)(rec_ratio2 / 10U), (unsigned)(rec_ratio2 % 10U),
        (unsigned)UACM_RECORD_PCM_10MS_BYTES, (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)s_session[1].lossless_verify_ok,
        (unsigned)s_session[1].lossless_crc_fail,
        (unsigned)s_session[1].lossless_decode_fail,
        (unsigned)s_session[1].lossless_len_fail,
        (unsigned)s_session[1].lossless_decode_max_us,
        (unsigned)s_session[1].lossless_decode_probe_est_max_us,
        (unsigned)s_session[1].lossless_decode_probe_count,
        (unsigned)s_session[1].lossless_decode_probe_fail,
        (unsigned)UACM_LOSSLESS_RECORD_WARN_US,
        (unsigned)s_session[1].lossless_decode_slow_hits);
}

static void uacm_log_record48_check(void)
{
    uint32_t total1 = s_session[0].lossless_comp_packets + s_session[0].lossless_raw_packets;
    uint32_t total2 = s_session[1].lossless_comp_packets + s_session[1].lossless_raw_packets;
    uint32_t avg1 = total1 ? (s_session[0].lossless_payload_bytes / total1) : 0U;
    uint32_t avg2 = total2 ? (s_session[1].lossless_payload_bytes / total2) : 0U;
    uint32_t ratio1 = s_session[0].lossless_raw_equiv_bytes ?
        (uint32_t)(((uint64_t)s_session[0].lossless_payload_bytes * 1000ULL) /
                   (uint64_t)s_session[0].lossless_raw_equiv_bytes) : 0U;
    uint32_t ratio2 = s_session[1].lossless_raw_equiv_bytes ?
        (uint32_t)(((uint64_t)s_session[1].lossless_payload_bytes * 1000ULL) /
                   (uint64_t)s_session[1].lossless_raw_equiv_bytes) : 0U;
    uint32_t fail1 = s_session[0].lossless_crc_fail + s_session[0].lossless_decode_fail + s_session[0].lossless_len_fail;
    uint32_t fail2 = s_session[1].lossless_crc_fail + s_session[1].lossless_decode_fail + s_session[1].lossless_len_fail;

    dbg("UACM REC20 raw20=%uB T1 group20/fb10=%u/%u avg=%uB ratio=%u.%u%% same=%s ok=%u fail=%u/%u/%u decmax=%uus | T2 group20/fb10=%u/%u avg=%uB ratio=%u.%u%% same=%s ok=%u fail=%u/%u/%u decmax=%uus\n",
        (unsigned)UACM_RECORD_GROUP_RAW_BYTES,
        (unsigned)s_session[0].record_group20_packets,
        (unsigned)s_session[0].record_fallback10_packets,
        (unsigned)avg1, (unsigned)(ratio1 / 10U), (unsigned)(ratio1 % 10U),
        (s_session[0].lossless_verify_ok && !fail1) ? "YES" : (fail1 ? "NO" : "NA"),
        (unsigned)s_session[0].lossless_verify_ok,
        (unsigned)s_session[0].lossless_crc_fail,
        (unsigned)s_session[0].lossless_decode_fail,
        (unsigned)s_session[0].lossless_len_fail,
        (unsigned)s_session[0].lossless_decode_max_us,
        (unsigned)s_session[1].record_group20_packets,
        (unsigned)s_session[1].record_fallback10_packets,
        (unsigned)avg2, (unsigned)(ratio2 / 10U), (unsigned)(ratio2 % 10U),
        (s_session[1].lossless_verify_ok && !fail2) ? "YES" : (fail2 ? "NO" : "NA"),
        (unsigned)s_session[1].lossless_verify_ok,
        (unsigned)s_session[1].lossless_crc_fail,
        (unsigned)s_session[1].lossless_decode_fail,
        (unsigned)s_session[1].lossless_len_fail,
        (unsigned)s_session[1].lossless_decode_max_us);
}

static void uacm_log_short_record_check(void)
{
    /* UARTSAFE2: this is intentionally short.  It does not run the codec or
     * touch the PCM rings; it only snapshots counters already maintained by
     * the record decoder.  raw/dec plus len_fail=0 proves the decoded length
     * stayed at the configured raw PCM length, while match + crc_fail=0 checks
     * the decoded PCM content against the sender's source CRC32. */
    dbg("UACM LLCHK R1 raw/dec=%u/%u ok=%u fail=%u/%u/%u | R2 raw/dec=%u/%u ok=%u fail=%u/%u/%u t=%u\n",
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)s_session[0].lossless_verify_ok,
        (unsigned)s_session[0].lossless_crc_fail,
        (unsigned)s_session[0].lossless_decode_fail,
        (unsigned)s_session[0].lossless_len_fail,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)s_session[1].lossless_verify_ok,
        (unsigned)s_session[1].lossless_crc_fail,
        (unsigned)s_session[1].lossless_decode_fail,
        (unsigned)s_session[1].lossless_len_fail,
        (unsigned)rtos_now(0));
}

static void uacm_record_task(void *arg)
{
    int record_listen = -1;
    uint8_t rr_first = 0U;
    uint32_t i;

    (void)arg;
    dbg("UACM RECORD mature-pipeline task prio=%u rcvbuf=%u budget=%u ring=%ums prebuffer=%ums\n",
        (unsigned)UACM_RECORD_TASK_PRIO,
        (unsigned)UACM_RECORD_SOCKET_BUF_BYTES,
        (unsigned)UACM_RECORD_RX_BUDGET_READS,
        (unsigned)(UACM_SESSION_RX_RING_BYTES / UACM_PCM_5MS_BYTES * 5U),
        (unsigned)(UACM_RX_PREBUFFER_BYTES / UACM_PCM_5MS_BYTES * 5U));

    while (1) {
        uint32_t now_ms = uacm_now_ms();

        if (s_ap_rx_agg_update_pending) {
            s_ap_rx_agg_update_pending = 0U;
            uacm_apply_record_rx_agg_mode(0U);
            s_ap_rx_agg_last_apply_ms = now_ms;
            s_ap_rx_agg_disabled = 0U;
            dbg("UACM WIFI record RX AMPDU enabled\n");
        }

        if (record_listen < 0) {
            record_listen = uacm_open_listen(UACM_RECORD_PORT, "record");
        }
        (void)uacm_accept_hello(record_listen, AUDIO_DIR_STA_TO_AP, "record");

        {
            uint8_t full_duplex = (s_usb_spk_on && s_usb_mic_on) ? 1U : 0U;
            uint32_t rx_budget = full_duplex ?
                UACM_RECORD_RX_FD_BUDGET_READS : UACM_RECORD_RX_BUDGET_READS;
            uint32_t sessions_this_pass = full_duplex ?
                UACM_RECORD_RX_FD_SESSIONS_PER_LOOP : UACM_SESSION_COUNT;

            for (i = 0U; i < sessions_this_pass; i++) {
                uacm_session_t *sess = &s_session[(rr_first + i) & 1U];
                if ((sess->record_fd >= 0) &&
                    (uacm_service_rx(sess, rx_budget) != 0)) {
                    if (!sess->record_down_reported) {
                        dbg("UACM WARN triangle%u record disconnected\n",
                            (unsigned)sess->client_id);
                        sess->record_down_reported = 1U;
                    }
                    uacm_close_record(sess);
                }
            }
        }
        /* In full duplex this alternates STA1/STA2 every 1 ms.  In record-only
         * mode both sessions are still serviced on every pass, only the order
         * changes as before. */
        rr_first ^= 1U;
        rtos_task_suspend(1U);
    }
}

static void uacm_network_task(void *arg)
{
    int play_listen = -1;
    uint32_t last_recovery_ms = 0U;
    uint32_t last_audio_ms = 0U;
    uint32_t prev_play[UACM_SESSION_COUNT] = { 0U, 0U };
    uint32_t prev_play_drop[UACM_SESSION_COUNT] = { 0U, 0U };
    uint32_t prev_record_wire[UACM_SESSION_COUNT] = { 0U, 0U };
    uint32_t prev_record_drop[UACM_SESSION_COUNT] = { 0U, 0U };
    uint32_t prev_gap[UACM_SESSION_COUNT] = { 0U, 0U };
    uint32_t prev_reord_cache = 0U;
    uint32_t prev_reord_force = 0U;
    uint32_t prev_reord_bypass = 0U;
    uint32_t prev_reord_old = 0U;
    uint32_t prev_reord_low = 0U;
    uint32_t prev_reord_limit = 0U;
    uint8_t prev_flow_state[UACM_SESSION_COUNT * 2U] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    uint8_t diag_run_seen = 0U;
    uint32_t last_llchk_ms = 0U;
    uint32_t abnormal_begin_ms = 0U; /* R19-AUTOREBOOT1 playback-abnormal onset */
    uint32_t i;

    (void)arg;
    play_listen = uacm_open_listen(UACM_PLAYBACK_PORT, "playback");
    dbg("UACM READY network %s ports playback=%u record=%u\n",
        UACM_VERSION, (unsigned)UACM_PLAYBACK_PORT,
        (unsigned)UACM_RECORD_PORT);
    while (1) {
        uint32_t now_ms = uacm_now_ms();

        if (play_listen < 0) {
            play_listen = uacm_open_listen(UACM_PLAYBACK_PORT, "playback");
        }
        (void)uacm_accept_hello(play_listen, AUDIO_DIR_AP_TO_STA, "playback");

        {
            static uint8_t rr;
            uint8_t order0 = rr & 1U;
            uint8_t order1 = order0 ^ 1U;
            uint8_t order[UACM_SESSION_COUNT];
            uint8_t full_duplex = (s_usb_spk_on && s_usb_mic_on) ? 1U : 0U;
            order[0] = order0;
            order[1] = order1;

            /* R8: record RX is owned by the dedicated high-priority record
             * task.  This task only services AP->STA playback, so a USB speaker
             * burst cannot delay microphone socket draining. */
            uacm_enqueue_playback(full_duplex);
            (void)uacm_prepare_pcm_pair();
            for (i = 0U; i < UACM_SESSION_COUNT; i++) {
                uacm_session_t *sess = &s_session[order[i]];
                int tx_ret = 0;
                if ((sess->playback_fd >= 0) &&
                    ((tx_ret = uacm_service_tx(sess, now_ms)) != 0)) {
                    if (!sess->playback_down_reported) {
                        dbg("UACM WARN triangle%u playback disconnected\n",
                            (unsigned)sess->client_id);
                        sess->playback_down_reported = 1U;
                    }
                    uacm_close_playback(sess);
                } 
            }
            rr ^= 1U;
        }

        {
            uint8_t flow_state[UACM_SESSION_COUNT * 2U];
            uint8_t state_changed = 0U;
            uint8_t any_stream_on = (s_usb_spk_on || s_usb_mic_on) ? 1U : 0U;

            for (i = 0U; i < UACM_SESSION_COUNT; i++) {
                flow_state[i * 2U] = !s_usb_spk_on ? UACM_FLOW_OFF :
                    ((s_session[i].playback_fd >= 0) ?
                     UACM_FLOW_ACTIVE : UACM_FLOW_DOWN);
                flow_state[i * 2U + 1U] = !s_usb_mic_on ? UACM_FLOW_OFF :
                    ((s_session[i].record_fd >= 0) ?
                     UACM_FLOW_ACTIVE : UACM_FLOW_DOWN);
                if ((flow_state[i * 2U] != prev_flow_state[i * 2U]) ||
                    (flow_state[i * 2U + 1U] != prev_flow_state[i * 2U + 1U])) {
                    state_changed = 1U;
                }
            }

            if ((last_audio_ms == 0U) || state_changed) {
                for (i = 0U; i < UACM_SESSION_COUNT; i++) {
                    prev_play[i] = s_session[i].playback_packets;
                    prev_play_drop[i] = s_session[i].playback_drop;
                    prev_record_wire[i] = s_session[i].record_wire_packets;
                    prev_record_drop[i] = s_session[i].record_drop;
                    prev_gap[i] = s_session[i].seq_gap;
                    prev_flow_state[i * 2U] = flow_state[i * 2U];
                    prev_flow_state[i * 2U + 1U] = flow_state[i * 2U + 1U];
                }
                {
                    uint16_t reord_q, reord_peak;
                    rwnx_reord_v533_diag_snapshot(&reord_q, &reord_peak,
                        &prev_reord_cache, &prev_reord_force,
                        &prev_reord_bypass, &prev_reord_old,
                        &prev_reord_low, &prev_reord_limit);
                    (void)reord_q;
                    (void)reord_peak;
                }
                last_audio_ms = now_ms;
            } else if (any_stream_on &&
                       ((now_ms - last_audio_ms) >= UACM_AUDIO_LOG_MS)) {
                uint32_t elapsed_ms = now_ms - last_audio_ms;
                uint32_t play_actual[UACM_SESSION_COUNT];
                uint32_t record_actual[UACM_SESSION_COUNT];
                uint8_t play_abnormal = 0U;
                char t1_play[24];
                char t1_record[24];
                char t2_play[24];
                char t2_record[24];
                uint16_t reord_q = 0U, reord_peak = 0U;
                uint32_t reord_cache = 0U, reord_force = 0U;
                uint32_t reord_bypass = 0U, reord_old = 0U;
                uint32_t reord_low = 0U, reord_limit = 0U;

                for (i = 0U; i < UACM_SESSION_COUNT; i++) {
                    uint32_t play_done = uacm_rate_per_second(
                        s_session[i].playback_packets - prev_play[i], elapsed_ms);
                    uint32_t play_loss = uacm_rate_per_second(
                        s_session[i].playback_drop - prev_play_drop[i], elapsed_ms);
                    uint32_t record_done = uacm_rate_per_second(
                        s_session[i].record_wire_packets - prev_record_wire[i], elapsed_ms);
                    uint32_t record_drop_blocks =
                        s_session[i].record_drop - prev_record_drop[i];
                    uint32_t record_drop_packets =
                        (record_drop_blocks + UACM_RECORD_BLOCKS_PER_PACKET - 1U) /
                        UACM_RECORD_BLOCKS_PER_PACKET;
                    uint32_t record_loss = uacm_rate_per_second(
                        record_drop_packets +
                        (s_session[i].seq_gap - prev_gap[i]), elapsed_ms);

                    play_actual[i] = uacm_effective_packets(
                        play_done, play_loss, UACM_PLAY_EXPECTED_PPS);
                    record_actual[i] = uacm_effective_packets(
                        record_done, record_loss, UACM_RECORD_EXPECTED_PPS);

                    /* DOWN means the STA is disconnected and is expected
                     * during a reconnect.  Only a connected playback stream
                     * whose packet rate is below the target is abnormal. */
                    if ((flow_state[i * 2U] == UACM_FLOW_ACTIVE) &&
                        (play_actual[i] != UACM_PLAY_EXPECTED_PPS)) {
                        play_abnormal = 1U;
                    }
                }

                uacm_format_flow(t1_play, sizeof(t1_play), flow_state[0],
                                 play_actual[0], UACM_PLAY_EXPECTED_PPS);
                uacm_format_flow(t1_record, sizeof(t1_record), flow_state[1],
                                 record_actual[0], UACM_RECORD_EXPECTED_PPS);
                uacm_format_flow(t2_play, sizeof(t2_play), flow_state[2],
                                 play_actual[1], UACM_PLAY_EXPECTED_PPS);
                uacm_format_flow(t2_record, sizeof(t2_record), flow_state[3],
                                 record_actual[1], UACM_RECORD_EXPECTED_PPS);

                rwnx_reord_v533_diag_snapshot(&reord_q, &reord_peak,
                    &reord_cache, &reord_force, &reord_bypass, &reord_old,
                    &reord_low, &reord_limit);

                if (play_abnormal) {
                    dbg(UACM_LOG_RED
                        "erro:UACM AUDIO T1 play=%s rec=%s | T2 play=%s rec=%s t=%u src=%u\n"
                        UACM_LOG_RESET,
                        t1_play, t1_record, t2_play, t2_record,
                        (unsigned)rtos_now(0), (unsigned)now_ms);
                }else{

                    dbg("UACM AUDIO T1 play=%s rec=%s | T2 play=%s rec=%s t=%u src=%u\n",
                        t1_play, t1_record, t2_play, t2_record,
                        (unsigned)rtos_now(0), (unsigned)now_ms);

                }
                
                if (s_usb_mic_on) {
                    uacm_log_record48_check();
                }

                prev_reord_cache = reord_cache;
                prev_reord_force = reord_force;
                prev_reord_bypass = reord_bypass;
                prev_reord_old = reord_old;
                prev_reord_low = reord_low;
                prev_reord_limit = reord_limit;

                for (i = 0U; i < UACM_SESSION_COUNT; i++) {
                    prev_play[i] = s_session[i].playback_packets;
                    prev_play_drop[i] = s_session[i].playback_drop;
                    prev_record_wire[i] = s_session[i].record_wire_packets;
                    prev_record_drop[i] = s_session[i].record_drop;
                    prev_gap[i] = s_session[i].seq_gap;
                }
                last_audio_ms = now_ms;
            }
        }
        rtos_task_suspend(UACM_NET_SLEEP_MS);
    }
}

static uint64_t uacm_block_power(const int16_t *pcm)
{
    uint64_t sum = 0ULL;
    uint32_t i;
    for (i = 0U; i < UACM_SAMPLES_5MS; i++) {
        int32_t v = pcm[i];
        sum += (uint64_t)(v * v);
    }
    return sum / UACM_SAMPLES_5MS;
}

static uint32_t uacm_block_peak(const int16_t *pcm)
{
    uint32_t peak = 0U;
    uint32_t i;

    for (i = 0U; i < UACM_SAMPLES_5MS; i++) {
        int32_t v = pcm[i];
        uint32_t a = (uint32_t)((v < 0) ? -v : v);
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static int16_t uacm_ring_peek_s16(const uacm_ring_t *ring, uint32_t sample_index)
{
    uint32_t byte_offset = sample_index * sizeof(int16_t);
    uint32_t pos = (ring->read_pos + byte_offset) % ring->capacity;
    uint16_t raw;

    if ((pos + 1U) < ring->capacity) {
        raw = (uint16_t)ring->storage[pos] |
              ((uint16_t)ring->storage[pos + 1U] << 8);
    } else {
        raw = (uint16_t)ring->storage[pos] |
              ((uint16_t)ring->storage[0] << 8);
    }
    return (int16_t)raw;
}

static int uacm_render_mic_frames(uacm_ring_t *ring, int16_t *dst,
                                  uint32_t source_frames)
{
    uint32_t out_frame;
    uint32_t consume_bytes = source_frames * UAC_BRIDGE_FRAME_BYTES;

    if ((source_frames < (UACM_FRAMES_5MS - 1U)) ||
        (source_frames > (UACM_FRAMES_5MS + 1U)) ||
        (uacm_ring_used(ring) < consume_bytes)) {
        return -1;
    }
    if (source_frames == UACM_FRAMES_5MS) {
        return (uacm_ring_read(ring, (uint8_t *)dst,
                               UACM_PCM_5MS_BYTES) == UACM_PCM_5MS_BYTES) ? 0 : -1;
    }

    for (out_frame = 0U; out_frame < UACM_FRAMES_5MS; out_frame++) {
        uint32_t numerator = out_frame * (source_frames - 1U);
        uint32_t idx = numerator / (UACM_FRAMES_5MS - 1U);
        uint32_t rem = numerator % (UACM_FRAMES_5MS - 1U);
        uint32_t frac_q15 = (rem << 15) / (UACM_FRAMES_5MS - 1U);
        uint32_t idx1 = (idx + 1U < source_frames) ? (idx + 1U) : idx;
        uint32_t ch;

        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            int32_t a = uacm_ring_peek_s16(ring, idx * UAC_BRIDGE_CHANNELS + ch);
            int32_t b = uacm_ring_peek_s16(ring, idx1 * UAC_BRIDGE_CHANNELS + ch);
            dst[out_frame * UAC_BRIDGE_CHANNELS + ch] =
                (int16_t)(a + (((b - a) * (int32_t)frac_q15) >> 15));
        }
    }
    (void)uacm_ring_read(ring, NULL, consume_bytes);
    return 0;
}

static int8_t uacm_choose_mic_drift(uacm_session_t *s,
                                      uint32_t used_frames)
{
    uint32_t average;
    uint32_t target_center;
    int32_t error;

    s->rx_drift_level_sum += used_frames;
    s->rx_drift_block_count++;
    if (s->rx_drift_block_count < UACM_RX_DRIFT_CHECK_BLOCKS) {
        return 0;
    }

    average = s->rx_drift_level_sum / s->rx_drift_block_count;
    target_center = (UACM_RX_PREBUFFER_BYTES / UAC_BRIDGE_FRAME_BYTES) +
                    (UACM_FRAMES_5MS / 2U);
    error = (int32_t)average - (int32_t)target_center;
    s->rx_drift_level_sum = 0U;
    s->rx_drift_block_count = 0U;

    if ((error > (int32_t)UACM_RX_DRIFT_DEADBAND_FRAMES) &&
        (used_frames >= (UACM_FRAMES_5MS + 1U))) {
        s->rx_drift_drop_frames++;
        return 1;
    }
    if ((error < -(int32_t)UACM_RX_DRIFT_DEADBAND_FRAMES) &&
        (used_frames >= (UACM_FRAMES_5MS - 1U))) {
        s->rx_drift_repeat_frames++;
        return -1;
    }
    return 0;
}

static uint8_t uacm_get_mic_block_locked(uacm_session_t *s, int16_t *pcm)
{
    uint32_t used = uacm_ring_used(&s->rx_ring);
    uint32_t used_frames = used / UAC_BRIDGE_FRAME_BYTES;
    uint32_t source_frames = UACM_FRAMES_5MS;

    if (!s->rx_started) {
        if (used < UACM_RX_PREBUFFER_BYTES) {
            memset(pcm, 0, UACM_PCM_5MS_BYTES);
            return 0U;
        }
        s->rx_started = 1U;
        s->rx_conceal_blocks = 0U;
        s->rx_drift_level_sum = 0U;
        s->rx_drift_block_count = 0U;
    }

    if (used_frames < UACM_FRAMES_5MS) {
        uint32_t available = used_frames;

        s->underflow += UACM_FRAMES_5MS - available;
        if (available >= (UACM_FRAMES_5MS / 2U)) {
            uint32_t frame;
            uint32_t tail = UACM_FRAMES_5MS - available;
            int32_t last_l;
            int32_t last_r;

            (void)uacm_ring_read(&s->rx_ring, (uint8_t *)pcm,
                                 available * UAC_BRIDGE_FRAME_BYTES);
            last_l = pcm[(available - 1U) * UAC_BRIDGE_CHANNELS];
            last_r = pcm[(available - 1U) * UAC_BRIDGE_CHANNELS + 1U];
            for (frame = available; frame < UACM_FRAMES_5MS; frame++) {
                uint32_t remain = UACM_FRAMES_5MS - frame;
                pcm[frame * UAC_BRIDGE_CHANNELS] =
                    (int16_t)((last_l * (int32_t)remain) / (int32_t)(tail + 1U));
                pcm[frame * UAC_BRIDGE_CHANNELS + 1U] =
                    (int16_t)((last_r * (int32_t)remain) / (int32_t)(tail + 1U));
            }
            s->rx_last_output[0] = pcm[(UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_CHANNELS];
            s->rx_last_output[1] = pcm[(UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_CHANNELS + 1U];
            s->rx_last_output_valid = 1U;
            s->rx_conceal_blocks = 0U;
            return 1U;
        }

        if (available != 0U) {
            (void)uacm_ring_read(&s->rx_ring, NULL,
                                 available * UAC_BRIDGE_FRAME_BYTES);
        }
        if (s->rx_conceal_blocks < 0xFFU) {
            s->rx_conceal_blocks++;
        }
        if (s->rx_conceal_blocks <= UACM_RX_CONCEAL_EMPTY_BLOCKS) {
            uint32_t frame;
            int32_t last_l = s->rx_last_output_valid ? s->rx_last_output[0] : 0;
            int32_t last_r = s->rx_last_output_valid ? s->rx_last_output[1] : 0;

            for (frame = 0U; frame < UACM_FRAMES_5MS; frame++) {
                uint32_t remain = UACM_FRAMES_5MS - 1U - frame;
                if (s->rx_conceal_blocks == 1U) {
                    pcm[frame * UAC_BRIDGE_CHANNELS] =
                        (int16_t)((last_l * (int32_t)remain) /
                                  (int32_t)UACM_FRAMES_5MS);
                    pcm[frame * UAC_BRIDGE_CHANNELS + 1U] =
                        (int16_t)((last_r * (int32_t)remain) /
                                  (int32_t)UACM_FRAMES_5MS);
                } else {
                    pcm[frame * UAC_BRIDGE_CHANNELS] = 0;
                    pcm[frame * UAC_BRIDGE_CHANNELS + 1U] = 0;
                }
            }
            s->rx_last_output[0] = 0;
            s->rx_last_output[1] = 0;
            s->rx_last_output_valid = 1U;
            return 1U;
        }

        s->rx_started = 0U;
        s->rx_conceal_blocks = 0U;
        s->rx_last_output_valid = 0U;
        s->rx_drift_level_sum = 0U;
        s->rx_drift_block_count = 0U;
        s->rx_rebuffer_count++;
        memset(pcm, 0, UACM_PCM_5MS_BYTES);
        return 0U;
    }

    {
        int8_t correction = uacm_choose_mic_drift(s, used_frames);
        source_frames = (uint32_t)((int32_t)UACM_FRAMES_5MS + correction);
    }
    if (used_frames < source_frames) {
        source_frames = UACM_FRAMES_5MS - 1U;
    }

    if (uacm_render_mic_frames(&s->rx_ring, pcm, source_frames) == 0) {
        s->rx_conceal_blocks = 0U;
        s->rx_last_output[0] = pcm[(UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_CHANNELS];
        s->rx_last_output[1] = pcm[(UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_CHANNELS + 1U];
        s->rx_last_output_valid = 1U;
        return 1U;
    }

    s->underflow++;
    s->rx_started = 0U;
    s->rx_rebuffer_count++;
    memset(pcm, 0, UACM_PCM_5MS_BYTES);
    return 0U;
}

static uint8_t uacm_get_mic_block(uacm_session_t *s, int16_t *pcm)
{
    uint8_t valid;
    uacm_rx_lock(s);
    valid = uacm_get_mic_block_locked(s, pcm);
    uacm_rx_unlock(s);
    return valid;
}

static uint64_t uacm_ema_u64(uint64_t current, uint64_t sample, uint32_t shift)
{
    if (sample >= current) {
        return current + ((sample - current) >> shift);
    }
    return current - ((current - sample) >> shift);
}

static void uacm_update_vad(uacm_session_t *s, uint8_t valid,
                            const int16_t *pcm)
{
    uint64_t power;
    uint64_t open_threshold;
    uint64_t close_threshold;

    if (!valid) {
        if (s->active_hold > 0U) {
            s->active_hold--;
        } else {
            s->active = 0U;
        }
        return;
    }

    power = uacm_block_power(pcm);
    s->level_power = uacm_ema_u64(s->level_power, power, 3U);
    if (!s->active) {
        s->noise_power = uacm_ema_u64(s->noise_power, power, 6U);
    }
    open_threshold = s->noise_power * 16ULL;
    close_threshold = s->noise_power * 6ULL;
    if (open_threshold < UACM_VAD_OPEN_MIN_POWER) {
        open_threshold = UACM_VAD_OPEN_MIN_POWER;
    }
    if (close_threshold < UACM_VAD_CLOSE_MIN_POWER) {
        close_threshold = UACM_VAD_CLOSE_MIN_POWER;
    }

    if (power >= open_threshold) {
        s->active = 1U;
        s->active_hold = UACM_VAD_HOLD_BLOCKS;
    } else if (s->active) {
        if (power >= close_threshold) {
            s->active_hold = UACM_VAD_HOLD_BLOCKS;
        } else if (s->active_hold > 0U) {
            s->active_hold--;
        } else {
            s->active = 0U;
        }
    }
}

static uint32_t uacm_smooth_gain(uint32_t current, uint32_t target)
{
    uint32_t delta;
    if (target > current) {
        delta = target - current;
        current += (delta + 3U) / 4U;   /* approximately 20 ms attack */
    } else if (current > target) {
        delta = current - target;
        current -= (delta + 31U) / 32U; /* approximately 160 ms release */
    }
    return current;
}

static int16_t uacm_sat_s16(int64_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static void uacm_reset_mic_lpf(uacm_mic_lpf_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

static void uacm_apply_mic_lpf(uacm_mic_lpf_state_t *state, int16_t *pcm)
{
    uint32_t frame;

    if ((state == NULL) || (pcm == NULL)) {
        return;
    }

    if (!state->valid) {
        uint32_t ch;
        uint32_t tap;

        /* Prime with the first real sample instead of zeros.  This avoids
         * creating an artificial fade/click when USB microphone streaming
         * starts or restarts. */
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            for (tap = 0U; tap < UACM_MIC_LPF_TAPS; tap++) {
                state->history[ch][tap] = pcm[ch];
            }
        }
        state->write_pos = 0U;
        state->valid = 1U;
    }

    for (frame = 0U; frame < UACM_FRAMES_5MS; frame++) {
        uint32_t ch;

        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            uint32_t sample_index = frame * UAC_BRIDGE_CHANNELS + ch;
            uint32_t tap;
            int32_t acc = UACM_MIC_LPF_ROUND;
            uint32_t center_index;

            state->history[ch][state->write_pos] = pcm[sample_index];

            /* Exploit coefficient symmetry: h[k] == h[N-1-k]. */
            for (tap = 0U; tap < UACM_MIC_LPF_HALF_TAPS; tap++) {
                uint32_t back_a = tap;
                uint32_t back_b = (UACM_MIC_LPF_TAPS - 1U) - tap;
                uint32_t index_a = state->write_pos + UACM_MIC_LPF_TAPS - back_a;
                uint32_t index_b = state->write_pos + UACM_MIC_LPF_TAPS - back_b;
                int32_t pair;

                if (index_a >= UACM_MIC_LPF_TAPS) {
                    index_a -= UACM_MIC_LPF_TAPS;
                }
                if (index_b >= UACM_MIC_LPF_TAPS) {
                    index_b -= UACM_MIC_LPF_TAPS;
                }
                pair = (int32_t)state->history[ch][index_a] +
                       (int32_t)state->history[ch][index_b];
                acc += pair * (int32_t)s_uacm_mic_lpf_q14[tap];
            }

            center_index = state->write_pos + UACM_MIC_LPF_TAPS -
                           UACM_MIC_LPF_HALF_TAPS;
            if (center_index >= UACM_MIC_LPF_TAPS) {
                center_index -= UACM_MIC_LPF_TAPS;
            }
            acc += (int32_t)state->history[ch][center_index] *
                   (int32_t)s_uacm_mic_lpf_q14[UACM_MIC_LPF_HALF_TAPS];
            pcm[sample_index] = uacm_sat_s16(acc >> UACM_MIC_LPF_Q);
        }

        state->write_pos++;
        if (state->write_pos >= UACM_MIC_LPF_TAPS) {
            state->write_pos = 0U;
        }
    }
}

/* Repair only a large discontinuity at the boundary between two 5 ms
 * blocks.  The correction starts at the previous block's final sample and
 * falls linearly to zero across the first 1 ms of the new block.  Ordinary
 * speech samples inside the block are never hard-limited.
 *
 * The 7.2 kHz FIR runs after this function, so any residual edge produced by
 * the correction is low-pass filtered before the block reaches USB. */
static uint32_t uacm_declick_block_boundary(uacm_boundary_state_t *state,
                                             int16_t *pcm,
                                             uint32_t *max_jump)
{
    uint32_t hits = 0U;
    uint32_t observed_max = 0U;
    uint32_t ch;

    if ((state == NULL) || (pcm == NULL)) {
        if (max_jump != NULL) {
            *max_jump = 0U;
        }
        return 0U;
    }

    if (state->valid) {
        for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
            int32_t first = pcm[ch];
            int32_t previous = state->last_sample[ch];
            int32_t jump = first - previous;
            uint32_t magnitude = (uint32_t)((jump < 0) ? -jump : jump);

            if (magnitude > observed_max) {
                observed_max = magnitude;
            }

            if (magnitude > UACM_BOUNDARY_DECLICK_THRESHOLD) {
                int32_t correction = previous - first;
                int64_t correction_q16 = (int64_t)correction * 65536LL;
                int64_t step_q16 = correction_q16 /
                                   (int64_t)UACM_BOUNDARY_DECLICK_DENOM;
                uint32_t frame;

                /* Only one constant division is done for a triggered channel.
                 * The 48-frame ramp itself uses adds and shifts, keeping the
                 * exceptional path much lighter than the removed all-sample
                 * hard slew limiter. */
                for (frame = 0U;
                     frame < UACM_BOUNDARY_DECLICK_FRAMES;
                     frame++) {
                    uint32_t index = frame * UAC_BRIDGE_CHANNELS + ch;
                    int32_t delta;
                    int32_t corrected;

                    if (frame == (UACM_BOUNDARY_DECLICK_FRAMES - 1U)) {
                        delta = 0;
                    } else if (correction_q16 >= 0) {
                        delta = (int32_t)((correction_q16 + 32768LL) >> 16);
                    } else {
                        delta = -(int32_t)(((-correction_q16) + 32768LL) >> 16);
                    }
                    corrected = (int32_t)pcm[index] + delta;
                    pcm[index] = uacm_sat_s16(corrected);
                    correction_q16 -= step_q16;
                }
                hits++;
            }
        }
    }

    for (ch = 0U; ch < UAC_BRIDGE_CHANNELS; ch++) {
        state->last_sample[ch] =
            pcm[(UACM_FRAMES_5MS - 1U) * UAC_BRIDGE_CHANNELS + ch];
    }
    state->valid = 1U;

    if (max_jump != NULL) {
        *max_jump = observed_max;
    }
    return hits;
}

static void uacm_mix_task(void *arg)
{
    int16_t pcm1[UACM_SAMPLES_5MS] __attribute__((aligned(4)));
    int16_t pcm2[UACM_SAMPLES_5MS] __attribute__((aligned(4)));
    int16_t mixed[UACM_SAMPLES_5MS] __attribute__((aligned(4)));
    uacm_boundary_state_t boundary_state = { { 0, 0 }, 0U };
    uint32_t diag_last_ms = 0U;
    uint32_t diag_valid1 = 0U;
    uint32_t diag_valid2 = 0U;
    uint32_t diag_active1 = 0U;
    uint32_t diag_active2 = 0U;
    uint32_t diag_fallback = 0U;
    uint32_t diag_no_data = 0U;
    uint32_t diag_peak1 = 0U;
    uint32_t diag_peak2 = 0U;
    uint32_t diag_out_peak = 0U;
    uint32_t diag_declick_hits = 0U;
    uint32_t diag_boundary_max_jump = 0U;

    (void)arg;
    uacm_reset_mic_lpf(&s_mic_lpf_state);
    while (1) {
        /*
         * Pace the 5 ms mixer from the USB consumer clock instead of using
         * "work time + fixed 5 ms sleep".  The old loop was always slightly
         * slower than real time, so the 10 ms USB ring repeatedly drained and
         * the ISR had to conceal 1 ms underruns.
         *
         * With this guard, one 5 ms block is produced only after at least
         * 5 ms of ring space has been freed.  Steady-state queue depth is
         * therefore kept around 5-10 ms without consuming the STA jitter
         * rings faster than the USB clock.
         */
        if (!s_usb_mic_on || s_usb_mic_resetting) {
            uacm_reset_mic_lpf(&s_mic_lpf_state);
            boundary_state.valid = 0U;
            boundary_state.last_sample[0] = 0;
            boundary_state.last_sample[1] = 0;
            diag_last_ms = uacm_now_ms();
            diag_valid1 = 0U;
            diag_valid2 = 0U;
            diag_active1 = 0U;
            diag_active2 = 0U;
            diag_fallback = 0U;
            diag_no_data = 0U;
            diag_peak1 = 0U;
            diag_peak2 = 0U;
            diag_out_peak = 0U;
            diag_declick_hits = 0U;
            diag_boundary_max_jump = 0U;
            rtos_task_suspend(UACM_MIX_POLL_MS);
            continue;
        }

        if (uacm_ring_free(&s_usb_mic_ring) <
            UACM_USB_MIC_PRODUCE_BYTES) {
            rtos_task_suspend(UACM_MIX_POLL_MS);
            continue;
        }

        uint8_t valid1 = uacm_get_mic_block(&s_session[0], pcm1);
        uint8_t valid2 = uacm_get_mic_block(&s_session[1], pcm2);
        uint8_t use1;
        uint8_t use2;
        uint8_t fallback = 0U;
        uint32_t target1 = 0U;
        uint32_t target2 = 0U;
        uint32_t peak1 = valid1 ? uacm_block_peak(pcm1) : 0U;
        uint32_t peak2 = valid2 ? uacm_block_peak(pcm2) : 0U;
        uint32_t out_peak;
        uint32_t i;
        uint32_t sum_gain;
        uint32_t now_ms;

        uacm_update_vad(&s_session[0], valid1, pcm1);
        uacm_update_vad(&s_session[1], valid2, pcm2);
        use1 = valid1 && s_session[0].active;
        use2 = valid2 && s_session[1].active;

        if (use1 && !use2) {
            target1 = UACM_GAIN_ONE_Q15;
        } else if (!use1 && use2) {
            target2 = UACM_GAIN_ONE_Q15;
        } else if (use1 && use2) {
            uint64_t p1 = s_session[0].level_power;
            uint64_t p2 = s_session[1].level_power;
            uint64_t sum = p1 + p2;
            target1 = (sum == 0ULL) ? (UACM_GAIN_ONE_Q15 / 2U) :
                      (uint32_t)((p1 * UACM_GAIN_ONE_Q15) / sum);
            if (target1 < UACM_GAIN_MIN_DUAL_Q15) {
                target1 = UACM_GAIN_MIN_DUAL_Q15;
            }
            if (target1 > UACM_GAIN_MAX_DUAL_Q15) {
                target1 = UACM_GAIN_MAX_DUAL_Q15;
            }
            target2 = UACM_GAIN_ONE_Q15 - target1;
            s_mix_dual_blocks++;
        } else if (valid1 && valid2) {
            /* v7.0.12R1 continuous-record fallback:
             * Both TCP streams and both PCM blocks are valid, but the adaptive
             * VAD classified the current low-level audio as inactive.  The old
             * branch set both gains to zero and produced the multi-second exact
             * flat lines seen by the PC recorder even while rec=200/200.
             * Keep the original AutoMix decisions whenever either VAD is active;
             * only this all-inactive case falls back to an equal-power path. */
            target1 = UACM_GAIN_ONE_Q15 / 2U;
            target2 = UACM_GAIN_ONE_Q15 - target1;
            fallback = 1U;
            s_mix_fallback_blocks++;
        } else if (valid1) {
            target1 = UACM_GAIN_ONE_Q15;
            fallback = 1U;
            s_mix_fallback_blocks++;
        } else if (valid2) {
            target2 = UACM_GAIN_ONE_Q15;
            fallback = 1U;
            s_mix_fallback_blocks++;
        } else {
            s_mix_zero_blocks++;
        }

        s_session[0].gain_q15 = uacm_smooth_gain(s_session[0].gain_q15, target1);
        s_session[1].gain_q15 = uacm_smooth_gain(s_session[1].gain_q15, target2);
        sum_gain = s_session[0].gain_q15 + s_session[1].gain_q15;
        if (sum_gain > UACM_GAIN_ONE_Q15) {
            s_session[0].gain_q15 =
                (uint32_t)(((uint64_t)s_session[0].gain_q15 * UACM_GAIN_ONE_Q15) /
                           sum_gain);
            s_session[1].gain_q15 = UACM_GAIN_ONE_Q15 - s_session[0].gain_q15;
        }

        for (i = 0U; i < UACM_SAMPLES_5MS; i++) {
            int64_t acc = (int64_t)pcm1[i] * s_session[0].gain_q15 +
                          (int64_t)pcm2[i] * s_session[1].gain_q15;
            mixed[i] = uacm_sat_s16((acc + 16384) >> 15);
        }

        /* Correct only an abnormal 5 ms block boundary.  Do this before the
         * FIR so the low-pass remains the final waveform-shaping stage and
         * suppresses any residual broadband edge. */
        if (s_usb_mic_on) {
            uint32_t block_max_jump = 0U;
            uint32_t block_hits = uacm_declick_block_boundary(
                &boundary_state, mixed, &block_max_jump);
            diag_declick_hits += block_hits;
            if (block_max_jump > diag_boundary_max_jump) {
                diag_boundary_max_jump = block_max_jump;
            }
        } else {
            boundary_state.valid = 0U;
            boundary_state.last_sample[0] = 0;
            boundary_state.last_sample[1] = 0;
        }

#if UACM_RECORD_SAMPLE_RATE_HZ == 16000U
        /* Legacy 16 kHz wire only: suppress 3x interpolation images before USB. */
        uacm_apply_mic_lpf(&s_mic_lpf_state, mixed);
#endif
        /* REC20PPS1 native 48 kHz wire intentionally bypasses the 7.2 kHz FIR
         * so the USB recording keeps the source's 48 kHz-bandwidth content. */
        out_peak = uacm_block_peak(mixed);

        /* USB MIC is a strict single-producer/single-consumer ring.  Free
         * space was checked before consuming the two source blocks, so this
         * write should normally succeed.  Recheck stream/reset state to avoid
         * publishing a stale block across a USB stream transition. */
        if (!s_usb_mic_on || s_usb_mic_resetting) {
            uacm_reset_mic_lpf(&s_mic_lpf_state);
            boundary_state.valid = 0U;
            boundary_state.last_sample[0] = 0;
            boundary_state.last_sample[1] = 0;
        } else if (uacm_ring_write(&s_usb_mic_ring,
                                   (const uint8_t *)mixed,
                                   UACM_PCM_5MS_BYTES) != 0) {
            s_usb_mic_drop++;
        }
        s_mix_blocks++;

        if (valid1) {
            diag_valid1++;
        }
        if (valid2) {
            diag_valid2++;
        }
        if (use1) {
            diag_active1++;
        }
        if (use2) {
            diag_active2++;
        }
        if (fallback) {
            diag_fallback++;
        }
        if (!valid1 && !valid2) {
            diag_no_data++;
        }
        if (peak1 > diag_peak1) {
            diag_peak1 = peak1;
        }
        if (peak2 > diag_peak2) {
            diag_peak2 = peak2;
        }
        if (out_peak > diag_out_peak) {
            diag_out_peak = out_peak;
        }

        now_ms = uacm_now_ms();
        if (!s_usb_mic_on) {
            /* UARTSAFE2: MIX diagnostics are accumulated for the whole active
             * microphone run and printed only after USB mic streaming stops. */
            if ((diag_last_ms != 0U) &&
                ((diag_valid1 != 0U) || (diag_valid2 != 0U))) {
                dbg("UACM MIXSUM T1 valid=%u active=%u peak=%u gain=%u q=%ums plc=%u rebuf=%u drift=%u/%u | "
                    "T2 valid=%u active=%u peak=%u gain=%u q=%ums plc=%u rebuf=%u drift=%u/%u | "
                    "out=%u fallback=%u no_data=%u usbq=%u/%u declick=%u maxboundary=%u usbfix=%u/%u/%u\n",
                    (unsigned)diag_valid1, (unsigned)diag_active1,
                    (unsigned)diag_peak1, (unsigned)s_session[0].gain_q15,
                    (unsigned)(uacm_ring_used(&s_session[0].rx_ring) * 5U /
                               UACM_PCM_5MS_BYTES),
                    (unsigned)s_session[0].rx_plc_frames,
                    (unsigned)s_session[0].rx_rebuffer_count,
                    (unsigned)s_session[0].rx_drift_drop_frames,
                    (unsigned)s_session[0].rx_drift_repeat_frames,
                    (unsigned)diag_valid2, (unsigned)diag_active2,
                    (unsigned)diag_peak2, (unsigned)s_session[1].gain_q15,
                    (unsigned)(uacm_ring_used(&s_session[1].rx_ring) * 5U /
                               UACM_PCM_5MS_BYTES),
                    (unsigned)s_session[1].rx_plc_frames,
                    (unsigned)s_session[1].rx_rebuffer_count,
                    (unsigned)s_session[1].rx_drift_drop_frames,
                    (unsigned)s_session[1].rx_drift_repeat_frames,
                    (unsigned)diag_out_peak, (unsigned)diag_fallback,
                    (unsigned)diag_no_data,
                    (unsigned)(uacm_ring_used(&s_usb_mic_ring) /
                               UACM_PCM_1MS_BYTES),
                    (unsigned)(UACM_USB_MIC_RING_BYTES / UACM_PCM_1MS_BYTES),
                    (unsigned)diag_declick_hits,
                    (unsigned)diag_boundary_max_jump,
                    (unsigned)s_usb_mic_underflow_events,
                    (unsigned)s_usb_mic_rebuffer_wait_packets,
                    (unsigned)s_usb_mic_recoveries);
            }
            boundary_state.valid = 0U;
            boundary_state.last_sample[0] = 0;
            boundary_state.last_sample[1] = 0;
            diag_last_ms = 0U;
            diag_valid1 = 0U;
            diag_valid2 = 0U;
            diag_active1 = 0U;
            diag_active2 = 0U;
            diag_fallback = 0U;
            diag_no_data = 0U;
            diag_peak1 = 0U;
            diag_peak2 = 0U;
            diag_out_peak = 0U;
            diag_declick_hits = 0U;
            diag_boundary_max_jump = 0U;
        } else if (diag_last_ms == 0U) {
            diag_last_ms = now_ms;
        }

        /* No fixed 5 ms sleep here.  The next iteration is gated by USB ring
         * free space and sleeps in 1 ms polls until another complete 5 ms
         * producer block is needed. */
    }
}

static int uacm_wait_wifi_ready(void)
{
#if PLF_WIFI_STACK
#if (PLF_AIC8800)
    const int ready = 0;
#elif (PLF_AIC8800MC) || (PLF_AIC8800M40)
    const int ready = IPC_EMB_START;
#else
    const int ready = 0;
#endif
    while (wifi_get_init_status() != ready) {
        rtos_task_suspend(10U);
    }
#endif
    return 0;
}

static void uacm_user_task(void *arg)
{
    int ret;
    (void)arg;

    us_ticker_init();
    (void)uacm_wait_wifi_ready();
    uacm_init_sessions();
    uacm_reset_usb_speaker();
    uacm_reset_usb_microphone();

    dbg("UACM Receiver AP %s ready: 48k stereo, AP->STA playback=10ms/%uB/%upps, two triangles\n",
        UACM_VERSION, (unsigned)UACM_PLAYBACK_PCM_BYTES,
        (unsigned)UACM_PLAY_EXPECTED_PPS);
    set_ap_enable_he_rate(0);
    set_ap_enable_ht_40(0);
    set_ap_allow_sta_inactivity_s(3);
    wlan_ap_switch_channel(UACM_AP_CHANNEL);
    ret = wlan_start_ap(UACM_AP_BAND, (uint8_t *)UACM_WIFI_SSID,
                        (uint8_t *)UACM_WIFI_PASSWORD);
    if (ret != 0) {
        dbg("UACM ERROR AP start ret=%d\n", ret);
        rtos_task_delete(NULL);
        return;
    }
    user_sleep_allow(0);
    dbg("UACM READY AP ssid=%s channel_cfg=%u ip=192.168.88.1\n",
        UACM_WIFI_SSID, (unsigned)UACM_AP_CHANNEL);
    dbg("UACM AP sta_inactivity=3s\n");

    ret = rtos_task_create(uacm_network_task, "U18_PLAY", APPLICATION_TASK,
                           UACM_NET_TASK_STACK, NULL,
                           RTOS_TASK_PRIORITY(UACM_NET_TASK_PRIO), NULL);
    dbg("UACM AP playback task create ret=%d\n", ret);
    ret = rtos_task_create(uacm_record_task, "U18_REC", APPLICATION_TASK,
                           UACM_RECORD_TASK_STACK, NULL,
                           RTOS_TASK_PRIORITY(UACM_RECORD_TASK_PRIO), NULL);
    dbg("UACM AP record task create ret=%d\n", ret);
    dbg("UACM PLAYBACK P10 packet=%ums subframe=%uB target_pps=%u txring=%uB USB_source=1ms\n",
        (unsigned)UACM_PLAYBACK_PACKET_MS,
        (unsigned)UACM_PLAYBACK_PCM_BYTES,
        (unsigned)UACM_PLAY_EXPECTED_PPS,
        (unsigned)UACM_SESSION_TX_RING_BYTES);
    dbg("UACM RXBYP1 record_ampdu=ON reord_pressure_action=single_frame_forward hard_drop=0 global_bypass=0\n");
    dbg("UACM LL3-BPK2-48PREP1-CPUDIAG3-UARTSAFE2-PBUF3 bidir_lossless=delta1+64frame-bitpack-fast packet_type=0x%02x play_encode=%uB record_decode=%uB raw_fallback=1 crc32_wire=1 frame_independent=1\n",
        (unsigned)AUDIO_PACKET_TYPE_UAC_PCM_LOSSLESS,
        (unsigned)UACM_PLAYBACK_PCM_BYTES,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES);
    dbg("UACM LL3-BPK2 validation playback AP raw_crc carried to STA and checked after %uB decode; record STA raw_crc checked after AP %uB decode\n",
        (unsigned)UACM_PLAYBACK_PCM_BYTES,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES);
    dbg("UACM REC20PPS1-DRAMFIX1 current_record=%uHz/%uB direct48k=1 codec_block=%u wire_v=%u parser_max=current10ms\n",
        (unsigned)UACM_RECORD_SAMPLE_RATE_HZ,
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)UACM_LOSSLESS_BLOCK_FRAMES,
        (unsigned)UACM_LOSSLESS_VERSION);
    dbg("UACM CPUDIAG3-UARTSAFE2-PBUF3-HOSTBUF2-JCTRL3-NETDIAG2-PLAYFIX2-REC20PPS1 wall=authoritative_task_latency codec_probe=OFF long_uart=dump_after_stream_off mix_uart=dump_after_mic_off irq_mask=0 priority_change=play_2to3+record_rx_3to4 fd_rx_fair=1\n");
    dbg("UACM UARTSAFE2 audio_log=%ums rec_llchk=%ums long_lossless=after_stream_off codec_probe=OFF\n",
        (unsigned)UACM_AUDIO_LOG_MS,
        (unsigned)UACM_LLCHK_LOG_MS);
    dbg("UACM HOSTBUF2-JCTRL3-NETDIAG2-PLAYFIX2-REC20PPS1-DRAMFIX1 record_rcvbuf=%uB record_task_prio=%u fw_debug_mask=0x0000 rx_budget_idle=%u fd=%u reads fd_sessions=%u/1ms STA_timelock_adjust=4f STA_prebuffer=70ms STA_timelock_threshold=packet_bytes_60_70ms STA_ovf_diag=ON STA_rxgap_diag=ON\n",
        (unsigned)UACM_RECORD_SOCKET_BUF_BYTES,
        (unsigned)UACM_RECORD_TASK_PRIO,
        (unsigned)UACM_RECORD_RX_BUDGET_READS,
        (unsigned)UACM_RECORD_RX_FD_BUDGET_READS,
        (unsigned)UACM_RECORD_RX_FD_SESSIONS_PER_LOOP);
    dbg("UACM NETDIAG2-PLAYFIX2-REC20PPS1-DRAMFIX1 audio_fields=t_rtos/src_apclk txseq1/2 RD+=cache/force/bypass/old/low/limit q/peak; play_task_prio=3 record_task_prio=4 fd_rx_budget=2 fd_rx_sessions=1; REORD_V533 behavior unchanged\n");
    dbg("UACM R18 rollback_qos=1 reorder_timeout=20ms time_lock=1 record_wire=48k_stereo_16bit_10ms pcm=%uB wire_pps=100 internal_blocks_pps=200 upsample=OFF direct48k=1 usb=48k_stereo record_ampdu_restore=1 tx_stall_timer_fix=1 app_prio(play/record/mix)=%u/%u/%u wifi_tcpip=3 wifi_ipc_tx=4 record_rx_ampdu=ON record_rcvbuf=%u large_gap_keep_buffer=1 stale_drop_compatible=1 jitter=%ums prebuffer=%ums max=%ums conceal=%ums drift=1frame/%ums usbq=%ums mix_usbq_paced=1\n",
        (unsigned)UACM_RECORD_PCM_10MS_BYTES,
        (unsigned)UACM_NET_TASK_PRIO,
        (unsigned)UACM_RECORD_TASK_PRIO,
        (unsigned)UACM_MIX_TASK_PRIO,
        (unsigned)UACM_RECORD_SOCKET_BUF_BYTES,
        (unsigned)(UACM_SESSION_RX_RING_BYTES / UACM_PCM_5MS_BYTES * 5U),
        (unsigned)(UACM_RX_PREBUFFER_BYTES / UACM_PCM_5MS_BYTES * 5U),
        (unsigned)(UACM_RX_MAX_DELAY_BYTES / UACM_PCM_5MS_BYTES * 5U),
        (unsigned)(UACM_RX_CONCEAL_EMPTY_BLOCKS * 5U),
        (unsigned)(UACM_RX_DRIFT_CHECK_BLOCKS * 5U),
        (unsigned)(UACM_USB_MIC_RING_BYTES / UACM_PCM_1MS_BYTES));
    ret = rtos_task_create(uacm_mix_task, "U18_MIX", APPLICATION_TASK,
                           UACM_MIX_TASK_STACK, NULL,
                           RTOS_TASK_PRIORITY(UACM_MIX_TASK_PRIO), NULL);
    dbg("UACM AP mixer task create ret=%d\n", ret);
    dbg("UACM BOUNDARY DECLICK threshold=%u fade=%u frames lpf_last=1 all_sample_slew=0\n",
        (unsigned)UACM_BOUNDARY_DECLICK_THRESHOLD,
        (unsigned)UACM_BOUNDARY_DECLICK_FRAMES);
    dbg("UACM USBMIC SMOOTH fade=%u frames rebuffer=%ums abrupt_zero=0\n",
        (unsigned)UACM_USB_MIC_FADE_FRAMES,
        (unsigned)UACM_USB_MIC_REBUFFER_MS);
    dbg("UACM MIXPACE usbq_driven=1 block=5ms poll=%ums target=5-10ms fixed_sleep=0\n",
        (unsigned)UACM_MIX_POLL_MS);
#if UACM_RECORD_SAMPLE_RATE_HZ == 16000U
    dbg("UACM MICLPF fir=%u q=%u fs=48000 cutoff=%uHz source_nyquist=8000Hz static_dram=%uB\n",
        (unsigned)UACM_MIC_LPF_TAPS,
        (unsigned)UACM_MIC_LPF_Q,
        (unsigned)UACM_MIC_LPF_CUTOFF_HZ,
        (unsigned)sizeof(s_mic_lpf_state));
#else
    dbg("UACM MICLPF bypass=1 reason=native48k record_bandwidth=full_source\n");
#endif
    rtos_task_delete(NULL);
}

void user_code_entry(void)
{
    if (rtos_task_create(uacm_user_task, "U18_AP", USER_CODE_TASK,
                         2048U, NULL, TASK_PRIORITY_USER_CODE, NULL) != 0) {
        dbg("UACM ERROR receiver AP entry task create failed\n");
    }
}
