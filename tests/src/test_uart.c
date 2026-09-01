/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "dbg.h"
#include "uart1_api.h"
#include "uart_dma_api.h"

#ifdef CFG_TEST_UART
/*
 * MACROS
 ****************************************************************************************
 */
#define USER_PRINTF             dbg

/**
 ****************************************************************************************
 * @brief test task implementation.
 ****************************************************************************************
 */
void uart_test(void)
{
    unsigned char test_buf[] = {
        0x30, 0x30, 0x30, 0x31, 0x30, 0x32, 0x30, 0x33, 0x30, 0x34,
        0x30, 0x35, 0x30, 0x36, 0x30, 0x37, 0x30, 0x38, 0x30, 0x39,
        0x31, 0x30, 0x31, 0x31, 0x31, 0x32, 0x31, 0x33, 0x31, 0x34,
        0x31, 0x35, 0x31, 0x36, 0x31, 0x37, 0x31, 0x38, 0x31, 0x39,
        0x32, 0x30, 0x32, 0x31, 0x32, 0x32, 0x32, 0x33, 0x32, 0x34,
        0x32, 0x35, 0x32, 0x36, 0x32, 0x37, 0x32, 0x38, 0x32, 0x39,
        0x33, 0x30, 0x33, 0x31,/* 0x33, 0x32, 0x33, 0x33, 0x33, 0x34,
        0x33, 0x35, 0x33, 0x36, 0x33, 0x37, 0x33, 0x38, 0x33, 0x39,*/
    };
    unsigned int idx;
    USER_PRINTF("\nStart test case: uart\n");

    uart1_init();

    #if (UART_DMA_TEST_ENABLED)
    uart1_rxirqen_setf(0); //disable rx interrupt
    uart_dma_init(AIC_UART_TYPE_1);

    do {
        #if 1
        USER_PRINTF("\n\nbef read\n");
        // Read data
        uart_dma_read(AIC_UART_TYPE_1, (unsigned int *)test_buf, sizeof(test_buf));
        USER_PRINTF("read size=%d\n", sizeof(test_buf));
        for (idx = 0; idx < sizeof(test_buf); idx++) {
            USER_PRINTF(" %02x", test_buf[idx]);
        }
        #endif

        USER_PRINTF("\n\nbef write\n");
        idx = 0;
        while (idx < 0x02) {
            unsigned char tmp = test_buf[0] & 0xF0;
            test_buf[0] = tmp | (0x0F & idx++);
            // Write data
            uart_dma_write(AIC_UART_TYPE_1, (unsigned int *)test_buf, sizeof(test_buf));
        }
        USER_PRINTF("write size=%d\n", sizeof(test_buf));
        for (idx = 0; idx < sizeof(test_buf); idx++) {
            USER_PRINTF(" %02x", test_buf[idx]);
        }
    } while (1);
    #else  /* UART_DMA_TEST_ENABLED */
    uart1_puts("Hi uart!\r\n");
    register_uart1_rx_function(uart_rx_handler);
    for (idx = 0; idx < sizeof(test_buf); idx++) {
        uart1_putc(test_buf[idx]);
    }
    #endif /* UART_DMA_TEST_ENABLED */

    USER_PRINTF("\nuart test done\n");
}

#endif /* CFG_TEST_UART */
