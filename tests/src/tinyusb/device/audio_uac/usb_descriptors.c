#include <string.h>
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/audio/audio.h"

/*
 * UAC1 Full-Speed compatibility descriptor
 * - One AudioControl interface + two AudioStreaming interfaces
 * - Microphone IN  : 48 kHz, 16-bit, stereo, 192 bytes / 1 ms
 * - Speaker OUT   : 48 kHz, 16-bit, stereo, 192 bytes / 1 ms
 *
 * This avoids clock RANGE control handling on Windows and avoids High-Speed
 * bInterval=4 microframe scheduling on Linux/AIC8800 DWC2.
 */
#define USB_VID   0xCafe
#define USB_BCD   0x0200

#define _PID_MAP(itf, n)  ((CFG_TUD_##itf) << (n))
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | \
                           _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3) | \
                           _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5))

#ifndef TUSB_DESC_INTERFACE_ASSOCIATION
#define TUSB_DESC_INTERFACE_ASSOCIATION 0x0B
#endif
#ifndef TUSB_DESC_CS_INTERFACE
#define TUSB_DESC_CS_INTERFACE          0x24
#endif
#ifndef TUSB_DESC_CS_ENDPOINT
#define TUSB_DESC_CS_ENDPOINT           0x25
#endif
#ifndef U16_TO_U8S_LE
#define U16_TO_U8S_LE(x)   (uint8_t)((x) & 0xff), (uint8_t)(((x) >> 8) & 0xff)
#endif
#ifndef U24_TO_U8S_LE
#define U24_TO_U8S_LE(x)   (uint8_t)((x) & 0xff), (uint8_t)(((x) >> 8) & 0xff), (uint8_t)(((x) >> 16) & 0xff)
#endif

/* UAC1 descriptor subtypes */
#define UAC1_CS_AC_HEADER               0x01
#define UAC1_CS_AC_INPUT_TERMINAL       0x02
#define UAC1_CS_AC_OUTPUT_TERMINAL      0x03
#define UAC1_CS_AC_FEATURE_UNIT         0x06
#define UAC1_CS_AS_GENERAL              0x01
#define UAC1_CS_AS_FORMAT_TYPE          0x02
#define UAC1_CS_EP_GENERAL              0x01

#define UAC1_FORMAT_TYPE_I              0x01
#define UAC1_FORMAT_TAG_PCM             0x0001

#define UAC1_TERM_USB_STREAMING         0x0101
#define UAC1_TERM_MICROPHONE            0x0201
#define UAC1_TERM_SPEAKER               0x0301

/* Keep the same entity/interface IDs as tusb_main.c uses. */
#define UAC1_ENTITY_MIC_INPUT_TERMINAL       1
#define UAC1_ENTITY_MIC_FEATURE_UNIT         2
#define UAC1_ENTITY_MIC_OUTPUT_TERMINAL      3
#define UAC1_ENTITY_CLOCK                    4
#define UAC1_ENTITY_SPK_INPUT_TERMINAL       5
#define UAC1_ENTITY_SPK_FEATURE_UNIT         6
#define UAC1_ENTITY_SPK_OUTPUT_TERMINAL      7

#define ITF_NUM_AUDIO_CONTROL                0
#define ITF_NUM_AUDIO_STREAMING_MIC_IN       1
#define ITF_NUM_AUDIO_STREAMING_SPK_OUT      2
#define ITF_NUM_TOTAL                        3

#define EPNUM_AUDIO_IN                       0x81
#define EPNUM_AUDIO_OUT                      0x02
#define AUDIO_SAMPLE_RATE                    48000u
#define AUDIO_BYTES_PER_SAMPLE               2u
#define AUDIO_MIC_CHANNELS                   2u
#define AUDIO_SPK_CHANNELS                   2u
#define AUDIO_MIC_EP_SIZE                    192u
#define AUDIO_SPK_EP_SIZE                    192u
#define AUDIO_EP_INTERVAL                    1u   /* Full-Speed: 1 frame = 1 ms */

#define UAC1_AC_TOTAL_LEN                    (10 + 12 + 10 + 9 + 12 + 10 + 9)
#define UAC1_AS_ALT_LEN                      (9 + 9 + 7 + 11 + 9 + 7)
#define CONFIG_TOTAL_LEN                     (TUD_CONFIG_DESC_LEN + TUD_AUDIO_IO_BOX_DESC_LEN)

tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0300,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

uint8_t const desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* IAD: Audio function containing AC + two AS interfaces. */
    8, TUSB_DESC_INTERFACE_ASSOCIATION,
       ITF_NUM_AUDIO_CONTROL, 3,
       TUSB_CLASS_AUDIO, AUDIO_FUNCTION_SUBCLASS_UNDEFINED, 0x00, 0,

    /* Standard AudioControl interface, UAC1 */
    9, TUSB_DESC_INTERFACE,
       ITF_NUM_AUDIO_CONTROL, 0, 0,
       TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL, 0x00, 0,

    /* UAC1 Class-specific AC header */
    10, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_HEADER,
        U16_TO_U8S_LE(0x0100),
        U16_TO_U8S_LE(UAC1_AC_TOTAL_LEN),
        2,
        ITF_NUM_AUDIO_STREAMING_MIC_IN,
        ITF_NUM_AUDIO_STREAMING_SPK_OUT,

    /* MIC path: microphone -> feature unit -> USB streaming */
    12, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_INPUT_TERMINAL,
        UAC1_ENTITY_MIC_INPUT_TERMINAL,
        U16_TO_U8S_LE(UAC1_TERM_MICROPHONE),
        UAC1_ENTITY_MIC_OUTPUT_TERMINAL,
        AUDIO_MIC_CHANNELS,
        U16_TO_U8S_LE(0x0000),
        0x00,
        0x00,

    10, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_FEATURE_UNIT,
        UAC1_ENTITY_MIC_FEATURE_UNIT,
        UAC1_ENTITY_MIC_INPUT_TERMINAL,
        1,              /* bControlSize */
        0x03,           /* master: mute + volume */
        0x00,           /* channel 1 */
        0x00,           /* channel 2 */
        0x00,

    9, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_OUTPUT_TERMINAL,
       UAC1_ENTITY_MIC_OUTPUT_TERMINAL,
       U16_TO_U8S_LE(UAC1_TERM_USB_STREAMING),
       0x00,
       UAC1_ENTITY_MIC_FEATURE_UNIT,
       0x00,

    /* Speaker path: USB streaming -> feature unit -> speaker */
    12, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_INPUT_TERMINAL,
        UAC1_ENTITY_SPK_INPUT_TERMINAL,
        U16_TO_U8S_LE(UAC1_TERM_USB_STREAMING),
        UAC1_ENTITY_SPK_OUTPUT_TERMINAL,
        AUDIO_SPK_CHANNELS,
        U16_TO_U8S_LE(0x0000),
        0x00,
        0x00,

    10, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_FEATURE_UNIT,
       UAC1_ENTITY_SPK_FEATURE_UNIT,
       UAC1_ENTITY_SPK_INPUT_TERMINAL,
       1,
       0x03,           /* master: mute + volume */
       0x00,           /* channel 1 */
       0x00,           /* channel 2 */
       0x00,

    9, TUSB_DESC_CS_INTERFACE, UAC1_CS_AC_OUTPUT_TERMINAL,
       UAC1_ENTITY_SPK_OUTPUT_TERMINAL,
       U16_TO_U8S_LE(UAC1_TERM_SPEAKER),
       0x00,
       UAC1_ENTITY_SPK_FEATURE_UNIT,
       0x00,

    /* MIC AS alt 0 */
    9, TUSB_DESC_INTERFACE,
       ITF_NUM_AUDIO_STREAMING_MIC_IN, 0, 0,
       TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, 0x00, 0,

    /* MIC AS alt 1 */
    9, TUSB_DESC_INTERFACE,
       ITF_NUM_AUDIO_STREAMING_MIC_IN, 1, 1,
       TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, 0x00, 0,

    7, TUSB_DESC_CS_INTERFACE, UAC1_CS_AS_GENERAL,
       UAC1_ENTITY_MIC_OUTPUT_TERMINAL,
       0x01,
       U16_TO_U8S_LE(UAC1_FORMAT_TAG_PCM),

    11, TUSB_DESC_CS_INTERFACE, UAC1_CS_AS_FORMAT_TYPE,
        UAC1_FORMAT_TYPE_I,
        AUDIO_MIC_CHANNELS,
        AUDIO_BYTES_PER_SAMPLE,
        AUDIO_BYTES_PER_SAMPLE * 8,
        1,
        U24_TO_U8S_LE(AUDIO_SAMPLE_RATE),

    9, TUSB_DESC_ENDPOINT,
       EPNUM_AUDIO_IN,
       0x05, /* Isochronous + async */
       U16_TO_U8S_LE(AUDIO_MIC_EP_SIZE),
       AUDIO_EP_INTERVAL,
       0x00,
       0x00,

    7, TUSB_DESC_CS_ENDPOINT, UAC1_CS_EP_GENERAL,
       0x01, /* sampling-frequency control */
       0x00,
       U16_TO_U8S_LE(0x0000),

    /* Speaker AS alt 0 */
    9, TUSB_DESC_INTERFACE,
       ITF_NUM_AUDIO_STREAMING_SPK_OUT, 0, 0,
       TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, 0x00, 0,

    /* Speaker AS alt 1 */
    9, TUSB_DESC_INTERFACE,
       ITF_NUM_AUDIO_STREAMING_SPK_OUT, 1, 1,
       TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, 0x00, 0,

    7, TUSB_DESC_CS_INTERFACE, UAC1_CS_AS_GENERAL,
       UAC1_ENTITY_SPK_INPUT_TERMINAL,
       0x01,
       U16_TO_U8S_LE(UAC1_FORMAT_TAG_PCM),

    11, TUSB_DESC_CS_INTERFACE, UAC1_CS_AS_FORMAT_TYPE,
        UAC1_FORMAT_TYPE_I,
        AUDIO_SPK_CHANNELS,
        AUDIO_BYTES_PER_SAMPLE,
        AUDIO_BYTES_PER_SAMPLE * 8,
        1,
        U24_TO_U8S_LE(AUDIO_SAMPLE_RATE),

    9, TUSB_DESC_ENDPOINT,
       EPNUM_AUDIO_OUT,
       0x09, /* Isochronous + adaptive */
       U16_TO_U8S_LE(AUDIO_SPK_EP_SIZE),
       AUDIO_EP_INTERVAL,
       0x00,
       0x00,

    7, TUSB_DESC_CS_ENDPOINT, UAC1_CS_EP_GENERAL,
       0x01, /* sampling-frequency control */
       0x00,
       U16_TO_U8S_LE(0x0000),
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

char const *string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },
    "AIC",
    "AIC UAC Full Duplex Bridge",
    "AICUAC3ST001",
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;
    size_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);

        if (chr_count > 32) {
            chr_count = 32;
        }

        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
