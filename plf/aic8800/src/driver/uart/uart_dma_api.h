/**
 ****************************************************************************************
 *
 * @file uart_dma_api.h
 *
 * @brief UART Driver APIs.
 *
 ****************************************************************************************
 */

#ifndef _UART_DMA_API_H_
#define _UART_DMA_API_H_

#define UART_DMA_TEST_ENABLED   (1 && defined(CFG_RTOS))
#define UART_DMA_ISR_ENABLED    (1 && defined(CFG_RTOS))

enum AIC_UART_TYPE
{
    AIC_UART_TYPE_1,
    AIC_UART_TYPE_2,
};

#if (UART_DMA_TEST_ENABLED)
void uart_dma_init(unsigned int uart_type);
int uart_dma_read(unsigned int uart_type, unsigned int *buff_rd, unsigned int byte_cnt);
int uart_dma_write(unsigned int uart_type, unsigned int *buff_wr, unsigned int byte_cnt);
#endif

#endif /* _UART_DMA_API_H_ */
