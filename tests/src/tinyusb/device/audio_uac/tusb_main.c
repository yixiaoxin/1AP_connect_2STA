#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "dbg.h"
#include "rtos.h"
#include "tusb.h"
#include "bsp/board_api.h"
#include "class/audio/audio.h"
#include "uac_audio_bridge.h"

#define UART_PRINT                         dbg
#define UAC_SAMPLE_RATE_HZ                 UAC_BRIDGE_SAMPLE_RATE_HZ
#define UAC_PACKET_BYTES                   UAC_BRIDGE_USB_PACKET_BYTES
#define UAC_MIC_PRIME_PACKETS              4U
#define UAC_ERROR_CHECK_MS                 1000U
#define UAC_ERROR_REPEAT_MS                30000U
#define UAC_USB_DIAG_LOG_MS                5000U
#define UAC_FLOW_GRACE_MS                  2500U
#define UAC_USB_RATE_MIN_PER_SEC           700U
#define UAC_USB_RATE_MAX_PER_SEC           1300U

#define ITF_NUM_AUDIO_STREAMING_MIC_IN      1U
#define ITF_NUM_AUDIO_STREAMING_SPK_OUT     2U
#define ENTITY_MIC_FEATURE_UNIT             2U
#define ENTITY_SPK_FEATURE_UNIT             6U
#define EP_AUDIO_MIC_IN                     0x81U
#define EP_AUDIO_SPK_OUT                    0x02U

#ifndef AUDIO10_CS_REQ_SET_CUR
#define AUDIO10_CS_REQ_SET_CUR              0x01U
#define AUDIO10_CS_REQ_GET_CUR              0x81U
#define AUDIO10_CS_REQ_GET_MIN              0x82U
#define AUDIO10_CS_REQ_GET_MAX              0x83U
#define AUDIO10_CS_REQ_GET_RES              0x84U
#endif

#ifndef AUDIO10_FU_CTRL_MUTE
#define AUDIO10_FU_CTRL_MUTE                0x01U
#define AUDIO10_FU_CTRL_VOLUME              0x02U
#endif

#ifndef AUDIO10_EP_CTRL_SAM_FREQ
#define AUDIO10_EP_CTRL_SAM_FREQ            0x01U
#endif

static volatile uint8_t s_mic_streaming = 0U;
static volatile uint8_t s_spk_streaming = 0U;
static volatile uint8_t s_mic_mute = 0U;
static volatile uint8_t s_spk_mute = 0U;
static volatile uint8_t s_usb_mounted = 0U;
static volatile int16_t s_mic_volume = 0;
static volatile int16_t s_spk_volume = 0;

static uint8_t s_mic_packet[UAC_PACKET_BYTES] CFG_TUSB_MEM_ALIGN;
static uint8_t s_spk_packet[UAC_PACKET_BYTES] CFG_TUSB_MEM_ALIGN;

static volatile uint32_t s_mic_tx_done = 0U;
static volatile uint32_t s_mic_tx_write = 0U;
static volatile uint32_t s_mic_tx_short = 0U;
static volatile uint32_t s_mic_tx_silence = 0U;
static volatile uint32_t s_spk_rx_done = 0U;
static volatile uint32_t s_spk_rx_forward = 0U;
static volatile uint32_t s_spk_rx_drop = 0U;
static volatile uint32_t s_spk_rx_short = 0U;

static uint32_t uac_usb_rate_per_second(uint32_t delta, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0U) {
        return 0U;
    }
    return (uint32_t)((((uint64_t)delta * 1000ULL) +
                       ((uint64_t)elapsed_ms / 2ULL)) /
                      (uint64_t)elapsed_ms);
}

static uint32_t uac_usb_effective_packets(uint32_t completed,
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

static uint32_t uac_usb_append_u32(char *dst, uint32_t dst_size,
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

static uint32_t uac_usb_append_text(char *dst, uint32_t dst_size,
                                    uint32_t pos, const char *text)
{
    while ((*text != '\0') && ((pos + 1U) < dst_size)) {
        dst[pos++] = *text++;
    }
    return pos;
}

static void uac_usb_format_flow(char *dst, uint32_t dst_size,
                                uint8_t active, uint32_t actual)
{
    uint32_t pos = 0U;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (!active) {
        pos = uac_usb_append_text(dst, dst_size, pos, "OFF");
    } else {
        pos = uac_usb_append_u32(dst, dst_size, pos, actual);
        pos = uac_usb_append_text(dst, dst_size, pos, "/1000");
    }
    dst[pos] = '\0';
}

static void uac_fill_mic_packet(void)
{
    int has_pcm = uac_bridge_usb_mic_render_isr(s_mic_packet,
                                                 sizeof(s_mic_packet));
    if (!has_pcm) {
        s_mic_tx_silence++;
    }

    /* Keep consuming the AP return stream while muted so the realtime queue
     * does not accumulate old PCM.  Only the USB-visible packet is zeroed. */
    if (s_mic_mute) {
        memset(s_mic_packet, 0, sizeof(s_mic_packet));
    }
}

static int uac_queue_one_mic_packet(void)
{
    uint16_t written;

    uac_fill_mic_packet();
    written = tud_audio_write(s_mic_packet, sizeof(s_mic_packet));
    if (written != sizeof(s_mic_packet)) {
        s_mic_tx_short++;
        return -1;
    }

    s_mic_tx_write++;
    return 0;
}

static void uac_prime_mic_stream(void)
{
    uint32_t i;

    tud_audio_clear_ep_in_ff();
    for (i = 0U; i < UAC_MIC_PRIME_PACKETS; i++) {
        if (uac_queue_one_mic_packet() != 0) {
            break;
        }
    }
}

static void uac_apply_stream_state(void)
{
    uac_bridge_usb_stream_state(s_mic_streaming, s_spk_streaming);
}

bool tud_audio_set_itf_cb(uint8_t rhport,
                          tusb_control_request_t const *request)
{
    uint8_t interface_number;
    uint8_t alternate_setting;

    (void)rhport;
    if (request == NULL) {
        return false;
    }

    interface_number = (uint8_t)request->wIndex;
    alternate_setting = (uint8_t)request->wValue;

    if (interface_number == ITF_NUM_AUDIO_STREAMING_MIC_IN) {
        uint8_t new_state = alternate_setting ? 1U : 0U;
        uint8_t changed = (new_state != s_mic_streaming);
        s_mic_streaming = new_state;
        uac_apply_stream_state();
        if (s_mic_streaming) {
            uac_prime_mic_stream();
        } else {
            tud_audio_clear_ep_in_ff();
        }
        if (changed) {
            UART_PRINT("UACM USB microphone %s\n",
                       s_mic_streaming ? "ON" : "OFF");
        }
        return true;
    }

    if (interface_number == ITF_NUM_AUDIO_STREAMING_SPK_OUT) {
        uint8_t new_state = alternate_setting ? 1U : 0U;
        uint8_t changed = (new_state != s_spk_streaming);
        s_spk_streaming = new_state;
        if (s_spk_streaming) {
            tud_audio_clear_ep_out_ff();
        }
        uac_apply_stream_state();
        if (changed) {
            UART_PRINT("UACM USB speaker %s\n",
                       s_spk_streaming ? "ON" : "OFF");
        }
        return true;
    }

    return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                   tusb_control_request_t const *request)
{
    uint8_t interface_number;

    (void)rhport;
    if (request == NULL) {
        return false;
    }

    interface_number = (uint8_t)request->wIndex;
    if (interface_number == ITF_NUM_AUDIO_STREAMING_MIC_IN) {
        s_mic_streaming = 0U;
        tud_audio_clear_ep_in_ff();
    } else if (interface_number == ITF_NUM_AUDIO_STREAMING_SPK_OUT) {
        s_spk_streaming = 0U;
        tud_audio_clear_ep_out_ff();
    }
    uac_apply_stream_state();
    return true;
}

static bool uac_control_get_feature(uint8_t rhport,
                                    tusb_control_request_t const *request,
                                    uint8_t entity_id,
                                    uint8_t control_selector)
{
    volatile uint8_t *mute_ptr;
    volatile int16_t *volume_ptr;
    static uint8_t mute_value;
    static int16_t volume_value;

    if (entity_id == ENTITY_MIC_FEATURE_UNIT) {
        mute_ptr = &s_mic_mute;
        volume_ptr = &s_mic_volume;
    } else if (entity_id == ENTITY_SPK_FEATURE_UNIT) {
        mute_ptr = &s_spk_mute;
        volume_ptr = &s_spk_volume;
    } else {
        return false;
    }

    if (control_selector == AUDIO10_FU_CTRL_MUTE) {
        if (request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
            mute_value = *mute_ptr ? 1U : 0U;
        } else if (request->bRequest == AUDIO10_CS_REQ_GET_MAX ||
                   request->bRequest == AUDIO10_CS_REQ_GET_RES) {
            mute_value = 1U;
        } else if (request->bRequest == AUDIO10_CS_REQ_GET_MIN) {
            mute_value = 0U;
        } else {
            return false;
        }
        return tud_control_xfer(rhport, request, &mute_value,
                                sizeof(mute_value));
    }

    if (control_selector == AUDIO10_FU_CTRL_VOLUME) {
        if (request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
            volume_value = *volume_ptr;
        } else if (request->bRequest == AUDIO10_CS_REQ_GET_MIN) {
            volume_value = (int16_t)(-60 * 256);
        } else if (request->bRequest == AUDIO10_CS_REQ_GET_MAX) {
            volume_value = 0;
        } else if (request->bRequest == AUDIO10_CS_REQ_GET_RES) {
            volume_value = (int16_t)(1 * 256);
        } else {
            return false;
        }
        return tud_control_xfer(rhport, request, &volume_value,
                                sizeof(volume_value));
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *request)
{
    uint8_t entity_id;
    uint8_t control_selector;

    if (request == NULL) {
        return false;
    }

    entity_id = TU_U16_HIGH(request->wIndex);
    control_selector = TU_U16_HIGH(request->wValue);
    return uac_control_get_feature(rhport, request,
                                   entity_id, control_selector);
}

bool tud_audio_get_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *request)
{
    (void)rhport;
    (void)request;
    return false;
}

static bool uac_control_get_sample_rate(uint8_t rhport,
                                        tusb_control_request_t const *request)
{
    static uint8_t sample_rate[3] = { 0x80U, 0xBBU, 0x00U }; /* 48000 */
    static uint8_t resolution[3] = { 0U, 0U, 0U };
    uint8_t endpoint;
    uint8_t control_selector;

    if (request == NULL) {
        return false;
    }

    endpoint = (uint8_t)request->wIndex;
    control_selector = TU_U16_HIGH(request->wValue);
    if ((endpoint != EP_AUDIO_MIC_IN && endpoint != EP_AUDIO_SPK_OUT) ||
        control_selector != AUDIO10_EP_CTRL_SAM_FREQ) {
        return false;
    }

    switch (request->bRequest) {
    case AUDIO10_CS_REQ_GET_CUR:
    case AUDIO10_CS_REQ_GET_MIN:
    case AUDIO10_CS_REQ_GET_MAX:
        return tud_control_xfer(rhport, request, sample_rate,
                                sizeof(sample_rate));
    case AUDIO10_CS_REQ_GET_RES:
        return tud_control_xfer(rhport, request, resolution,
                                sizeof(resolution));
    default:
        return false;
    }
}

bool tud_audio_get_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *request)
{
    return uac_control_get_sample_rate(rhport, request);
}

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *request,
                                 uint8_t *buffer)
{
    uint8_t entity_id;
    uint8_t control_selector;
    volatile uint8_t *mute_ptr;
    volatile int16_t *volume_ptr;

    (void)rhport;
    if ((request == NULL) || (buffer == NULL) ||
        (request->bRequest != AUDIO10_CS_REQ_SET_CUR)) {
        return true;
    }

    entity_id = TU_U16_HIGH(request->wIndex);
    control_selector = TU_U16_HIGH(request->wValue);
    if (entity_id == ENTITY_MIC_FEATURE_UNIT) {
        mute_ptr = &s_mic_mute;
        volume_ptr = &s_mic_volume;
    } else if (entity_id == ENTITY_SPK_FEATURE_UNIT) {
        mute_ptr = &s_spk_mute;
        volume_ptr = &s_spk_volume;
    } else {
        return true;
    }

    if ((control_selector == AUDIO10_FU_CTRL_MUTE) &&
        (request->wLength >= 1U)) {
        *mute_ptr = buffer[0] ? 1U : 0U;
    } else if ((control_selector == AUDIO10_FU_CTRL_VOLUME) &&
               (request->wLength >= 2U)) {
        *volume_ptr = (int16_t)((uint16_t)buffer[0] |
                                ((uint16_t)buffer[1] << 8));
    }

    if (entity_id == ENTITY_SPK_FEATURE_UNIT) {
        uac_bridge_usb_playback_ctrl(s_spk_volume, s_spk_mute);
    }
    return true;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *request,
                              uint8_t *buffer)
{
    (void)rhport;
    (void)request;
    (void)buffer;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *request,
                             uint8_t *buffer)
{
    uint8_t endpoint;
    uint8_t control_selector;
    uint32_t sample_rate;

    (void)rhport;
    if (request == NULL) {
        return false;
    }

    endpoint = (uint8_t)request->wIndex;
    control_selector = TU_U16_HIGH(request->wValue);
    if ((endpoint != EP_AUDIO_MIC_IN && endpoint != EP_AUDIO_SPK_OUT) ||
        control_selector != AUDIO10_EP_CTRL_SAM_FREQ ||
        request->bRequest != AUDIO10_CS_REQ_SET_CUR) {
        return true;
    }

    if ((buffer == NULL) || (request->wLength < 3U)) {
        return false;
    }

    sample_rate = (uint32_t)buffer[0] |
                  ((uint32_t)buffer[1] << 8) |
                  ((uint32_t)buffer[2] << 16);
    return sample_rate == UAC_SAMPLE_RATE_HZ;
}

/* Called after the Audio class has already scheduled the next EP81 transfer.
 * Refill exactly one packet into the TinyUSB software FIFO.  The callback can
 * run in the DWC2 ISR fast path, so it contains no socket calls or logging. */
bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t bytes_sent,
                           uint8_t function_id, uint8_t endpoint,
                           uint8_t alternate_setting)
{
    (void)rhport;
    (void)bytes_sent;
    (void)function_id;
    (void)endpoint;

    if (alternate_setting != 0U && s_mic_streaming) {
        s_mic_tx_done++;
        (void)uac_queue_one_mic_packet();
    }
    return true;
}

/* The Audio class has copied EP02 into its FIFO and immediately rearmed EP02
 * before this callback.  Drain the just-received packet into the lock-free
 * Wi-Fi ring so the TinyUSB FIFO cannot grow when recording is also active. */
bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t bytes_received,
                           uint8_t function_id, uint8_t endpoint,
                           uint8_t alternate_setting)
{
    uint16_t remaining;

    (void)rhport;
    (void)function_id;
    (void)endpoint;

    if (alternate_setting == 0U || !s_spk_streaming || bytes_received == 0U) {
        return true;
    }

    s_spk_rx_done++;
    remaining = bytes_received;
    while (remaining > 0U) {
        uint16_t request_bytes = remaining;
        uint16_t read_bytes;

        if (request_bytes > sizeof(s_spk_packet)) {
            request_bytes = sizeof(s_spk_packet);
        }
        read_bytes = tud_audio_read(s_spk_packet, request_bytes);
        if (read_bytes == 0U) {
            s_spk_rx_short++;
            break;
        }
        remaining = (uint16_t)(remaining - read_bytes);

        if (read_bytes != UAC_PACKET_BYTES) {
            s_spk_rx_short++;
            continue;
        }
        if (s_spk_mute) {
            memset(s_spk_packet, 0, sizeof(s_spk_packet));
        }
        if (uac_bridge_usb_speaker_push_isr(s_spk_packet,
                                             sizeof(s_spk_packet)) == 0) {
            s_spk_rx_forward++;
        } else {
            s_spk_rx_drop++;
        }
    }

    return true;
}

void tud_mount_cb(void)
{
    s_usb_mounted = 1U;
    UART_PRINT("UAC3: USB mounted\n");
}

void tud_umount_cb(void)
{
    s_usb_mounted = 0U;
    s_mic_streaming = 0U;
    s_spk_streaming = 0U;
    /* The class driver has already closed its endpoints.  Do not
     * touch class FIFOs after p_desc is cleared during unmount. */
    uac_apply_stream_state();
    UART_PRINT("UAC3: USB unmounted\n");
}

void tusb_audio_uac_demo(void)
{
    uint32_t last_error_ms = 0U;
    uint32_t last_error_report_ms = 0U;
    uint32_t mic_on_since_ms = 0U;
    uint32_t spk_on_since_ms = 0U;
    uint8_t last_mic_on = 0U;
    uint8_t last_spk_on = 0U;
    uint8_t last_error_mask = 0U;
    uint32_t err_mic_done = 0U;
    uint32_t err_spk_done = 0U;
    uint32_t err_mic_write = 0U;
    uint32_t err_spk_forward = 0U;
    uint32_t err_mic_short = 0U;
    uint32_t err_spk_drop = 0U;
    uint32_t err_spk_short = 0U;
    uint32_t diag_last_ms = 0U;
    uint32_t diag_mic_done = 0U;
    uint32_t diag_spk_forward = 0U;
    uint32_t diag_mic_short = 0U;
    uint32_t diag_mic_silence = 0U;
    uint32_t diag_spk_drop = 0U;
    uint32_t diag_spk_short = 0U;
    uint8_t diag_stream_mask = 0U;

    UART_PRINT("UACM Receiver USB v7.0.12R18 ready: record wire 16k stereo 10ms, USB/playback 48k stereo\n");
    UART_PRINT("UACM USB starting\n");

    rtos_task_set_priority(NULL, RTOS_TASK_PRIORITY(4));
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    UART_PRINT("UACM READY USB audio\n");

    last_error_ms = rtos_now(false);
    diag_last_ms = last_error_ms;
    mic_on_since_ms = last_error_ms;
    spk_on_since_ms = last_error_ms;

    while (1) {
        uint32_t now_ms;

        /* Isochronous callbacks only update counters and move PCM. */
        tud_task_ext(1U, false);
        now_ms = rtos_now(false);

        {
            uint8_t stream_mask = (uint8_t)((s_mic_streaming ? 1U : 0U) |
                                            (s_spk_streaming ? 2U : 0U));
            uint32_t diag_elapsed = now_ms - diag_last_ms;

            if (stream_mask != diag_stream_mask) {
                diag_stream_mask = stream_mask;
                diag_last_ms = now_ms;
                diag_mic_done = s_mic_tx_done;
                diag_spk_forward = s_spk_rx_forward;
                diag_mic_short = s_mic_tx_short;
                diag_mic_silence = s_mic_tx_silence;
                diag_spk_drop = s_spk_rx_drop;
                diag_spk_short = s_spk_rx_short;
            } else if ((stream_mask != 0U) &&
                       (diag_elapsed >= UAC_USB_DIAG_LOG_MS)) {
                uint32_t mic_done = uac_usb_rate_per_second(
                    s_mic_tx_done - diag_mic_done, diag_elapsed);
                uint32_t mic_loss = uac_usb_rate_per_second(
                    s_mic_tx_short - diag_mic_short, diag_elapsed);
                uint32_t mic_silence = uac_usb_rate_per_second(
                    s_mic_tx_silence - diag_mic_silence, diag_elapsed);
                uint32_t spk_done = uac_usb_rate_per_second(
                    s_spk_rx_forward - diag_spk_forward, diag_elapsed);
                uint32_t spk_loss = uac_usb_rate_per_second(
                    (s_spk_rx_drop - diag_spk_drop) +
                    (s_spk_rx_short - diag_spk_short), diag_elapsed);
                uint32_t mic_actual = uac_usb_effective_packets(
                    mic_done, mic_loss, 1000U);
                uint32_t spk_actual = uac_usb_effective_packets(
                    spk_done, spk_loss, 1000U);
                char mic_text[24];
                char spk_text[24];

                uac_usb_format_flow(mic_text, sizeof(mic_text),
                                    s_mic_streaming, mic_actual);
                uac_usb_format_flow(spk_text, sizeof(spk_text),
                                    s_spk_streaming, spk_actual);
                UART_PRINT("UACM USB mic=%s bridge_silence=%u/1000 mute=%u spk=%s\n",
                           mic_text, (unsigned)mic_silence,
                           (unsigned)s_mic_mute, spk_text);

                diag_last_ms = now_ms;
                diag_mic_done = s_mic_tx_done;
                diag_spk_forward = s_spk_rx_forward;
                diag_mic_short = s_mic_tx_short;
                diag_mic_silence = s_mic_tx_silence;
                diag_spk_drop = s_spk_rx_drop;
                diag_spk_short = s_spk_rx_short;
            }
        }

        if (s_mic_streaming != last_mic_on) {
            last_mic_on = s_mic_streaming;
            mic_on_since_ms = now_ms;
            err_mic_done = s_mic_tx_done;
            err_mic_write = s_mic_tx_write;
        }
        if (s_spk_streaming != last_spk_on) {
            last_spk_on = s_spk_streaming;
            spk_on_since_ms = now_ms;
            err_spk_done = s_spk_rx_done;
            err_spk_forward = s_spk_rx_forward;
        }

        if ((now_ms - last_error_ms) >= UAC_ERROR_CHECK_MS) {
            uint32_t elapsed = now_ms - last_error_ms;
            uint32_t d_mic_done = s_mic_tx_done - err_mic_done;
            uint32_t d_mic_write = s_mic_tx_write - err_mic_write;
            uint32_t d_spk_done = s_spk_rx_done - err_spk_done;
            uint32_t d_spk_forward = s_spk_rx_forward - err_spk_forward;
            uint32_t d_mic_short = s_mic_tx_short - err_mic_short;
            uint32_t d_spk_drop = s_spk_rx_drop - err_spk_drop;
            uint32_t d_spk_short = s_spk_rx_short - err_spk_short;
            uint32_t mic_done_rate = 0U;
            uint32_t mic_write_rate = 0U;
            uint32_t spk_done_rate = 0U;
            uint32_t spk_forward_rate = 0U;
            uint8_t error_mask = 0U;

            /* A few isolated isochronous drops are not actionable and used
             * to create ERROR/RECOVER spam.  Report only a real burst. */
            if (d_mic_short || d_spk_short || (d_spk_drop >= 10U)) {
                error_mask |= 0x01U; /* significant short/drop */
            }

            if (s_mic_streaming &&
                ((now_ms - mic_on_since_ms) >= UAC_FLOW_GRACE_MS)) {
                mic_done_rate = (d_mic_done * 1000U) / elapsed;
                mic_write_rate = (d_mic_write * 1000U) / elapsed;
                if ((mic_done_rate < UAC_USB_RATE_MIN_PER_SEC) ||
                    (mic_done_rate > UAC_USB_RATE_MAX_PER_SEC) ||
                    (mic_write_rate < UAC_USB_RATE_MIN_PER_SEC) ||
                    (mic_write_rate > UAC_USB_RATE_MAX_PER_SEC)) {
                    error_mask |= 0x02U; /* EP81 rate */
                }
            }

            if (s_spk_streaming &&
                ((now_ms - spk_on_since_ms) >= UAC_FLOW_GRACE_MS)) {
                spk_done_rate = (d_spk_done * 1000U) / elapsed;
                spk_forward_rate = (d_spk_forward * 1000U) / elapsed;
                if ((spk_done_rate < UAC_USB_RATE_MIN_PER_SEC) ||
                    (spk_done_rate > UAC_USB_RATE_MAX_PER_SEC) ||
                    (spk_forward_rate < UAC_USB_RATE_MIN_PER_SEC) ||
                    (spk_forward_rate > UAC_USB_RATE_MAX_PER_SEC)) {
                    error_mask |= 0x04U; /* EP02 rate */
                }
            }


            if (error_mask != 0U) {
                if ((error_mask != last_error_mask) ||
                    ((now_ms - last_error_report_ms) >= UAC_ERROR_REPEAT_MS)) {
                    UART_PRINT("UACM ERROR USB mask=0x%02X mic=%u/%u spk=%u/%u short=%u drop=%u/%u stream=%u/%u mounted=%u\n",
                               (unsigned)error_mask,
                               (unsigned)mic_done_rate,
                               (unsigned)mic_write_rate,
                               (unsigned)spk_done_rate,
                               (unsigned)spk_forward_rate,
                               (unsigned)d_mic_short,
                               (unsigned)d_spk_drop,
                               (unsigned)d_spk_short,
                               (unsigned)s_mic_streaming,
                               (unsigned)s_spk_streaming,
                               (unsigned)s_usb_mounted);
                    last_error_report_ms = now_ms;
                }
            } else if (last_error_mask != 0U) {
                UART_PRINT("UACM RECOVER USB previous_mask=0x%02X\n",
                           (unsigned)last_error_mask);
                last_error_report_ms = now_ms;
            }
            last_error_mask = error_mask;

            err_mic_done = s_mic_tx_done;
            err_mic_write = s_mic_tx_write;
            err_spk_done = s_spk_rx_done;
            err_spk_forward = s_spk_rx_forward;
            err_mic_short = s_mic_tx_short;
            err_spk_drop = s_spk_rx_drop;
            err_spk_short = s_spk_rx_short;
            last_error_ms = now_ms;
        }
    }
}

