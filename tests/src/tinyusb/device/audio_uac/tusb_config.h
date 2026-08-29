#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dbg.h"

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT                   0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED                OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                        OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG                     0
#endif

#define CFG_TUSB_DEBUG_PRINTF              dbg_test_print
#define CFG_TUD_ENABLED                    1
#define CFG_TUD_MAX_SPEED                  OPT_MODE_FULL_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN                 __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE             64
#endif

#ifndef CFG_TUD_ENDPOINT0_BUFSIZE
#define CFG_TUD_ENDPOINT0_BUFSIZE          64
#endif

#ifndef CFG_TUD_EDPT_DEDICATED_HWFIFO
#define CFG_TUD_EDPT_DEDICATED_HWFIFO      0
#endif

#define CFG_TUD_CDC                        0
#define CFG_TUD_MSC                        0
#define CFG_TUD_HID                        0
#define CFG_TUD_MIDI                       0
#define CFG_TUD_VENDOR                     0
#define CFG_TUD_AUDIO                      1

#define CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP  0
#define CFG_TUD_AUDIO_ENABLE_EP_IN         1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT        1

/* Hand-written UAC1 descriptor, excluding the 9-byte configuration header. */
#define TUD_AUDIO_IO_BOX_DESC_LEN          193
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN      TUD_AUDIO_IO_BOX_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT      2
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ   64
#define CFG_TUD_AUDIO_CTRL_BUF_SZ          64

/* Both USB directions and both Wi-Fi directions use exactly the same wire
 * format: 48 kHz, stereo, signed 16-bit little-endian.  One full-speed USB
 * frame therefore carries 48 stereo samples = 192 bytes. */
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          2
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX           192
/* Four packets: one active, at least two queued, one refill margin. */
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ        768

#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX          2
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX          192
/* EP02 is drained in its completion callback; four packets cover control-path
 * preemption without allowing USB latency to grow. */
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ       768

#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL            0
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP            0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
