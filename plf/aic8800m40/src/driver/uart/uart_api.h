/**
 ****************************************************************************************
 *
 * @file uart_api.h
 *
 * @brief UART Driver APIs. Below define is only for AIC8800M40
 *
 ****************************************************************************************
 */
#ifndef _UART_API_H_
#define _UART_API_H_

#include <stdint.h>

enum UART_TYPE
{
    UART_TYPE_1,
    UART_TYPE_2,
};

enum UART_CHANNEL
{
    UART1_CHANNEL_1,  // rx: A2,  tx: A3
    UART1_CHANNEL_2,  // rx: A4,  tx: A5
    UART1_CHANNEL_3,  // rx: A10, tx: A11
    UART2_CHANNEL_1,  // rx: A6,  tx: A7
    UART2_CHANNEL_2,  // rx: A10, tx: A11
};

typedef void (*uart_rx_func_t)(void);

typedef struct uart_cfg_info
{
    int uart_chan;
    uint32_t baudrate;
    uart_rx_func_t rx_func;
} uart_cfg;

int uart_init(int uart_type, uart_cfg *cfg);
void uart_deinit(int uart_type);
void uart_putc(int uart_type, char ch);
char uart_getc(int uart_type);
int uart_get_rx_count(int uart_type);

#endif /* _UART_API_H_ */

