/*
 * USB CDC-ACM Device bridge for AIC8800M40 btdm_wifi project.
 * Uses SDK USBD low-level symbols. Does not use TinyUSB.
 *
 * Debug version:
 * - Do not start CDC OUT RX in set configuration directly.
 * - Start CDC OUT RX after host opens the COM port (SET_CONTROL_LINE_STATE/DTR).
 * - Print all key return values.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "dbg.h"
#include "usbd_cdc_bridge.h"

#define CDC_PRINT                   dbg
#define USB_DESC_TYPE_DEVICE        0x01
#define USB_DESC_TYPE_CONFIGURATION 0x02
#define USB_DESC_TYPE_STRING        0x03
#define USB_DESC_TYPE_DEVICE_QUAL   0x06
#define USB_CLASS_MISC              0xEF
#define USB_SUBCLASS_COMMON         0x02
#define USB_PROTOCOL_IAD            0x01
#define USB_CLASS_CDC               0x02
#define USB_CDC_SUBCLASS_ACM        0x02
#define USB_CDC_PROTOCOL_AT         0x01
#define USB_CLASS_DATA              0x0A
#define CDC_REQ_SET_LINE_CODING        0x20
#define CDC_REQ_GET_LINE_CODING        0x21
#define CDC_REQ_SET_CONTROL_LINE_STATE 0x22
#define CDC_CTRL_ITF                0
#define CDC_DATA_ITF                1
#define CDC_EP_INT                  1
#define CDC_EP_BULK                 2
#define USB_EP_DIR_OUT              0
#define USB_EP_DIR_IN               1
#define USB_EP_TYPE_BULK            2
#define USB_EP_TYPE_INTR            3
#define CDC_INT_MPS                 8
#define CDC_BULK_MPS                64
#define CDC_RX_BUF_SIZE             64
#define CDC_TX_BUF_SIZE             64

extern int  hal_usb_open(const void *callbacks, int param);
extern int  hal_usb_activate_epn(uint8_t dir, uint8_t ep, uint8_t type, uint16_t mps);
extern int  hal_usb_recv_epn(uint8_t ep, uint8_t *buf, uint32_t len);
extern int  hal_usb_send_epn(uint8_t ep, const uint8_t *buf, uint32_t len, uint8_t zlp);

typedef struct {
    uint8_t  state;
    uint8_t  reserved0[3];
    uint8_t *data;
    uint16_t length;
    uint16_t reserved1;
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_ctrl_xfer_t;

typedef struct {
    const uint8_t *(*get_device_desc)(uint8_t type);
    const uint8_t *(*get_config_desc)(void);
    const uint8_t *(*get_string_desc)(uint8_t index);
    int (*setuprecv_handler)(usb_ctrl_xfer_t *xfer);
    int (*datarecv_handler)(usb_ctrl_xfer_t *xfer);
    int (*setcfg_handler)(uint8_t cfg);
    int (*set_itf_handler)(uint8_t itf, uint8_t alt);
    void *reserved_1c;
    void (*state_change_handler)(uint32_t state);
    void *reserved_24;
    void (*epbulk_recv_compl_handler)(uint8_t *buf, uint32_t len);
    void *reserved_2c;
    void *reserved_30;
    void *reserved_34;
    void (*epint_send_compl_handler)(uint8_t ep, uint32_t len);
    void (*epbulk_send_compl_handler)(uint8_t ep, uint32_t len);
    void *reserved_40;
    void *reserved_44;
    void *reserved_48;
} usb_dev_callbacks_t;

static volatile bool cdc_inited = false;
static volatile bool cdc_configured = false;
static volatile bool cdc_tx_busy = false;
static volatile bool cdc_rx_armed = false;
static volatile uint16_t cdc_ctrl_line_state = 0;
static uint8_t cdc_rx_buf[CDC_RX_BUF_SIZE];
static uint8_t cdc_tx_buf[CDC_TX_BUF_SIZE];

static uint8_t cdc_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };

static const uint8_t cdc_device_desc[] = {
    18, USB_DESC_TYPE_DEVICE, 0x00, 0x02,
    USB_CLASS_MISC, USB_SUBCLASS_COMMON, USB_PROTOCOL_IAD, 64,
    0xC0, 0xA1, 0x40, 0x88, 0x00, 0x01,
    1, 2, 3, 1
};

static const uint8_t cdc_device_qualifier[] = {
    10, USB_DESC_TYPE_DEVICE_QUAL, 0x00, 0x02,
    USB_CLASS_MISC, USB_SUBCLASS_COMMON, USB_PROTOCOL_IAD, 64, 1, 0
};

static const uint8_t cdc_config_desc[] = {
    9, USB_DESC_TYPE_CONFIGURATION, 75, 0x00, 2, 1, 0, 0x80, 50,
    8, 0x0B, CDC_CTRL_ITF, 2, USB_CLASS_CDC, USB_CDC_SUBCLASS_ACM, USB_CDC_PROTOCOL_AT, 0,
    9, 0x04, CDC_CTRL_ITF, 0, 1, USB_CLASS_CDC, USB_CDC_SUBCLASS_ACM, USB_CDC_PROTOCOL_AT, 4,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, CDC_DATA_ITF,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, CDC_CTRL_ITF, CDC_DATA_ITF,
    7, 0x05, 0x80 | CDC_EP_INT, USB_EP_TYPE_INTR, CDC_INT_MPS, 0x00, 16,
    9, 0x04, CDC_DATA_ITF, 0, 2, USB_CLASS_DATA, 0x00, 0x00, 0,
    7, 0x05, CDC_EP_BULK, USB_EP_TYPE_BULK, CDC_BULK_MPS, 0x00, 0,
    7, 0x05, 0x80 | CDC_EP_BULK, USB_EP_TYPE_BULK, CDC_BULK_MPS, 0x00, 0,
};

static const uint8_t string_langid[] = { 4, USB_DESC_TYPE_STRING, 0x09, 0x04 };
static const uint8_t string_manu[]   = { 16, USB_DESC_TYPE_STRING, 'A',0,'I',0,'C',0,'S',0,'e',0,'m',0,'i',0 };
static const uint8_t string_prod[]   = { 38, USB_DESC_TYPE_STRING, 'A',0,'I',0,'C',0,'8',0,'8',0,'0',0,'0',0,'M',0,'4',0,'0',0,' ',0,'C',0,'D',0,'C',0,' ',0,'D',0,'e',0,'v',0 };
static const uint8_t string_ser[]    = { 14, USB_DESC_TYPE_STRING, '0',0,'0',0,'0',0,'0',0,'0',0,'1',0 };
static const uint8_t string_itf[]    = { 16, USB_DESC_TYPE_STRING, 'C',0,'D',0,'C',0,' ',0,'A',0,'C',0,'M',0 };

static int cdc_start_rx(const char *tag)
{
    int ret;

    if (!cdc_configured) {
        CDC_PRINT("%s CDC RX skip: not configured\r\n", tag);
        return -1;
    }

    if (cdc_rx_armed) {
        CDC_PRINT("%s CDC RX skip: already armed\r\n", tag);
        return 0;
    }

    memset(cdc_rx_buf, 0, sizeof(cdc_rx_buf));
    CDC_PRINT("%s CDC RX start: ep=%d len=%d\r\n", tag, CDC_EP_BULK, CDC_RX_BUF_SIZE);

    ret = hal_usb_recv_epn(CDC_EP_BULK, cdc_rx_buf, sizeof(cdc_rx_buf));
    CDC_PRINT("%s CDC RX ret=%d\r\n", tag, ret);

    /* In this SDK, previous logs indicate ret=1 means RX did not work.
     * Treat ret=0 as successfully armed, but still print ret for verification.
     */
    if (ret == 0) {
        cdc_rx_armed = true;
    }

    return ret;
}

static const uint8_t *cdc_get_device_desc_local(uint8_t type)
{
    if (type == USB_DESC_TYPE_DEVICE) return cdc_device_desc;
    if (type == USB_DESC_TYPE_DEVICE_QUAL) return cdc_device_qualifier;
    return NULL;
}

static const uint8_t *cdc_get_config_desc_local(void) { return cdc_config_desc; }

static const uint8_t *cdc_get_string_desc_local(uint8_t index)
{
    switch (index) {
    case 0: return string_langid;
    case 1: return string_manu;
    case 2: return string_prod;
    case 3: return string_ser;
    case 4: return string_itf;
    default: return NULL;
    }
}

static int cdc_setuprecv_handler(usb_ctrl_xfer_t *xfer)
{
    if (!xfer) return 0;

    switch (xfer->bRequest) {
    case CDC_REQ_GET_LINE_CODING:
        CDC_PRINT("CDC GET_LINE_CODING\r\n");
        xfer->data = cdc_line_coding;
        xfer->length = sizeof(cdc_line_coding);
        xfer->state = 4;
        return 1;

    case CDC_REQ_SET_LINE_CODING:
        CDC_PRINT("CDC SET_LINE_CODING len=%d\r\n", xfer->wLength);
        xfer->data = cdc_line_coding;
        xfer->length = sizeof(cdc_line_coding);
        xfer->state = 3;
        return 1;

    case CDC_REQ_SET_CONTROL_LINE_STATE:
        cdc_ctrl_line_state = xfer->wValue;
        CDC_PRINT("CDC SET_CONTROL_LINE_STATE value=0x%x DTR=%d RTS=%d configured=%d\r\n",
                  cdc_ctrl_line_state,
                  (cdc_ctrl_line_state & 0x01) ? 1 : 0,
                  (cdc_ctrl_line_state & 0x02) ? 1 : 0,
                  cdc_configured);
        xfer->state = 6;

        /* Host opened the COM port when DTR=1. Start OUT RX here instead of
         * immediately in setcfg_handler, because some SDKs are not ready there.
         */
        if (cdc_ctrl_line_state & 0x03) {
            cdc_start_rx("DTR_OR_RTS");
        } else {
            cdc_rx_armed = false;
        }
        return 1;

    default:
        CDC_PRINT("CDC unknown setup req=0x%x type=0x%x value=0x%x index=%d len=%d\r\n",
                  xfer->bRequest, xfer->bmRequestType, xfer->wValue,
                  xfer->wIndex, xfer->wLength);
        return 0;
    }
}

static int cdc_datarecv_handler(usb_ctrl_xfer_t *xfer)
{
    if (xfer && xfer->bRequest == CDC_REQ_SET_LINE_CODING) {
        CDC_PRINT("CDC LINE_CODING set: %02x %02x %02x %02x %02x %02x %02x\r\n",
                  cdc_line_coding[0], cdc_line_coding[1], cdc_line_coding[2],
                  cdc_line_coding[3], cdc_line_coding[4], cdc_line_coding[5],
                  cdc_line_coding[6]);
    }
    return 1;
}

static int cdc_setcfg_handler(uint8_t cfg)
{
    int ret;

    if (cfg != 1) {
        cdc_configured = false;
        cdc_rx_armed = false;
        return 0;
    }

    CDC_PRINT("CDC set configuration %d\r\n", cfg);

    ret = hal_usb_activate_epn(USB_EP_DIR_IN, CDC_EP_INT, USB_EP_TYPE_INTR, CDC_INT_MPS);
    CDC_PRINT("activate INT IN ret=%d\r\n", ret);

    ret = hal_usb_activate_epn(USB_EP_DIR_IN, CDC_EP_BULK, USB_EP_TYPE_BULK, CDC_BULK_MPS);
    CDC_PRINT("activate BULK IN ret=%d\r\n", ret);

    ret = hal_usb_activate_epn(USB_EP_DIR_OUT, CDC_EP_BULK, USB_EP_TYPE_BULK, CDC_BULK_MPS);
    CDC_PRINT("activate BULK OUT ret=%d\r\n", ret);

    cdc_configured = true;
    cdc_rx_armed = false;

    /* Do not call hal_usb_recv_epn() here. It previously returned 1 and no OUT
     * callback was triggered. RX will start after SET_CONTROL_LINE_STATE/DTR.
     */
    CDC_PRINT("CDC configured, wait DTR to start RX\r\n");

    return 1;
}

static int cdc_set_itf_handler(uint8_t itf, uint8_t alt)
{
    CDC_PRINT("CDC set interface itf=%d alt=%d\r\n", itf, alt);
    if (cdc_configured && (cdc_ctrl_line_state & 0x01)) {
        cdc_start_rx("SET_ITF");
    }
    return 1;
}

static void cdc_state_change_handler(uint32_t state)
{
    CDC_PRINT("CDC state change %d\r\n", state);
    if (state == 0) {
        cdc_configured = false;
        cdc_rx_armed = false;
        cdc_tx_busy = false;
        cdc_ctrl_line_state = 0;
    }
}

static void cdc_epbulk_recv_compl_handler(uint8_t *buf, uint32_t len)
{
    uint32_t rx_len = len;
    const uint8_t *rx_ptr = buf ? buf : cdc_rx_buf;

    cdc_rx_armed = false;

    CDC_PRINT("CDC OUT ENTER buf=%p len=%d\r\n", buf, len);

    if (rx_len > CDC_RX_BUF_SIZE) {
        rx_len = CDC_RX_BUF_SIZE;
    }

    if (rx_len > 0) {
        CDC_PRINT("CDC OUT rx len=%d\r\n", rx_len);

        if (usbd_cdc_bridge_rx_hook) {
            usbd_cdc_bridge_rx_hook(rx_ptr, rx_len);
        }
    }

    cdc_start_rx("RESTART");
}

static void cdc_epbulk_send_compl_handler(uint8_t ep, uint32_t len)
{
    CDC_PRINT("CDC IN complete ep=%d len=%d\r\n", ep, len);
    cdc_tx_busy = false;
}

static void cdc_epint_send_compl_handler(uint8_t ep, uint32_t len)
{
    (void)ep;
    (void)len;
}

static const usb_dev_callbacks_t cdc_callbacks = {
    cdc_get_device_desc_local, cdc_get_config_desc_local, cdc_get_string_desc_local,
    cdc_setuprecv_handler, cdc_datarecv_handler, cdc_setcfg_handler, cdc_set_itf_handler,
    NULL, cdc_state_change_handler, NULL, cdc_epbulk_recv_compl_handler,
    NULL, NULL, NULL, cdc_epint_send_compl_handler, cdc_epbulk_send_compl_handler,
    NULL, NULL, NULL,
};

void usbd_cdc_bridge_init(void)
{
    int ret;

    if (cdc_inited) return;

    CDC_PRINT("\r\nUSBD CDC bridge init start\r\n");
    ret = hal_usb_open(&cdc_callbacks, 0);
    CDC_PRINT("hal_usb_open ret=%d\r\n", ret);

    cdc_inited = true;
    CDC_PRINT("USBD CDC bridge init done\r\n");
}

int usbd_cdc_bridge_send(const uint8_t *buf, uint32_t len)
{
    int ret;

    if (!cdc_inited || !cdc_configured || !buf || !len) {
        CDC_PRINT("CDC send reject: inited=%d configured=%d buf=%p len=%d\r\n",
                  cdc_inited, cdc_configured, buf, len);
        return -1;
    }

    if (cdc_tx_busy) {
        CDC_PRINT("CDC send reject: tx busy\r\n");
        return -2;
    }

    if (len > CDC_TX_BUF_SIZE) {
        len = CDC_TX_BUF_SIZE;
    }

    memcpy(cdc_tx_buf, buf, len);
    cdc_tx_busy = true;

    ret = hal_usb_send_epn(CDC_EP_BULK, cdc_tx_buf, len,
                           (len == CDC_BULK_MPS) ? 1 : 0);
    if (ret != 0) {
        CDC_PRINT("CDC hal_usb_send_epn failed ret=%d\r\n", ret);
        cdc_tx_busy = false;
    } else {
        CDC_PRINT("CDC IN send start len=%d\r\n", len);
    }

    return ret;
}

