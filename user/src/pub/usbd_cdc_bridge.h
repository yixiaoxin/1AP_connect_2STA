#ifndef USBD_CDC_BRIDGE_H
#define USBD_CDC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usbd_cdc_bridge_init(void);
int usbd_cdc_bridge_send(const uint8_t *buf, uint32_t len);
void usbd_cdc_bridge_rx_hook(const uint8_t *buf, uint32_t len) __attribute__((weak));

#ifdef __cplusplus
}
#endif

#endif /* USBD_CDC_BRIDGE_H */
