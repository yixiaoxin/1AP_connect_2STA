/**
 ****************************************************************************************
 *
 * @file uart_dma_api.c
 *
 * @brief UART Driver APIs.
 *
 ****************************************************************************************
 */
#include "dbg.h"
#include "uart1_api.h"
#include "uart2_api.h"
#include "reg_sysctrl.h"
#include "dma_generic.h"
#include "uart_dma_api.h"
#if UART_DMA_TEST_ENABLED
#include "rtos_al.h"
#endif

/*
 * MACROS
 ****************************************************************************************
 */
#define UART_DMA_BYTE_CNT_MAX   (64)
#define UART_RXDMA_CH1_IDX      DMA_CHANNEL_UART_RX
#define UART_TXDMA_CH1_IDX      DMA_CHANNEL_UART_TX
#define UART_RXDMA_IRQ1_IDX     DMA08_IRQn
#define UART_TXDMA_IRQ1_IDX     DMA09_IRQn

#define UART_RXDMA_CH2_IDX      DMA_CHANNEL_UART_RX // If use uart1 and uart2 at the same
#define UART_TXDMA_CH2_IDX      DMA_CHANNEL_UART_TX // time, need to modify CH2 DMA channel.
#define UART_RXDMA_IRQ2_IDX     DMA08_IRQn          // If use uart1 and uart2 at the same
#define UART_TXDMA_IRQ2_IDX     DMA09_IRQn          // time, need to modify CH2 DMA IRQn.

/*
 * GLOBAL CONSTANTS & VARIABLES
 ****************************************************************************************
 */
#if (UART_DMA_TEST_ENABLED)
rtos_semaphore uart_rd_sema;
rtos_semaphore uart_wr_sema;
void uart1_rxdma_isr(void);
void uart1_txdma_isr(void);
void uart2_rxdma_isr(void);
void uart2_txdma_isr(void);
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */

#if (UART_DMA_TEST_ENABLED)
/**
 ****************************************************************************************
 * @brief init func
 ****************************************************************************************
 */
void uart_dma_init(unsigned int uart_type)
{
    int ret;
    uint32_t dma_tx_irq, dma_rx_irq;
    ret = rtos_semaphore_create(&uart_rd_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    ret = rtos_semaphore_create(&uart_wr_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    // clk en
    cpusysctrl_hclkme_set(CSC_HCLKME_DMA_EN_BIT);
    if (uart_type == AIC_UART_TYPE_1) {
        dma_rx_irq = UART_RXDMA_IRQ1_IDX;
        dma_tx_irq = UART_TXDMA_IRQ1_IDX;
        NVIC_SetVector(dma_rx_irq, (uint32_t)uart1_rxdma_isr);
        NVIC_SetPriority(dma_rx_irq, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(dma_rx_irq);
        NVIC_SetVector(dma_tx_irq, (uint32_t)uart1_txdma_isr);
        NVIC_SetPriority(dma_tx_irq, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(dma_tx_irq);
    } else if (uart_type == AIC_UART_TYPE_2) {
        dma_rx_irq = UART_RXDMA_IRQ2_IDX;
        dma_tx_irq = UART_TXDMA_IRQ2_IDX;
        NVIC_SetVector(dma_rx_irq, (uint32_t)uart2_rxdma_isr);
        NVIC_SetPriority(dma_rx_irq, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(dma_rx_irq);
        NVIC_SetVector(dma_tx_irq, (uint32_t)uart2_txdma_isr);
        NVIC_SetPriority(dma_tx_irq, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(dma_tx_irq);
    } else {
        dbg("uart_type err\n");
    }
}

/**
 ****************************************************************************************
 * @brief read func, blocking type
 ****************************************************************************************
 */
int uart_dma_read(unsigned int uart_type, unsigned int *buff_rd, unsigned int byte_cnt)
{
    int ch = 0;
    byte_cnt = MIN(byte_cnt, UART_DMA_BYTE_CNT_MAX);

    if (uart_type == AIC_UART_TYPE_1) {
        ch = UART_RXDMA_CH1_IDX;
        dma_erqcsr_set(REQ_CID_UART1_RX, ch);
    } else if (uart_type == AIC_UART_TYPE_2) {
        ch = UART_RXDMA_CH2_IDX;
        dma_erqcsr_set(REQ_CID_UART2_RX, ch);
    } else {
        dbg("uart_type err\n");
    }

    #if (PLF_AIC8800)
    dma_ch_rqr_erqm_setb(ch);
    #else
    dma_ch_rqr_erqm_clrb(ch);
    #endif
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(UART1_TXRXD_ADDR));
    dma_ch_tbl0cr_set(ch, ((1) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, byte_cnt);
    dma_ch_tbl2cr_set(ch, byte_cnt);
    dma_ch_tsr_set(ch, ((1 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if UART_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));
    #if UART_DMA_ISR_ENABLED
    rtos_semaphore_wait(uart_rd_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    #endif

    return 0;
}

/**
 ****************************************************************************************
 * @brief write func, blocking type
 ****************************************************************************************
 */
int uart_dma_write(unsigned int uart_type, unsigned int *buff_wr, unsigned int byte_cnt)
{
    int ch = 0;
    byte_cnt = MIN(byte_cnt, UART_DMA_BYTE_CNT_MAX);

    if (uart_type == AIC_UART_TYPE_1) {
        ch = UART_TXDMA_CH1_IDX;
        dma_erqcsr_set(REQ_CID_UART1_TX, ch);
    } else if (uart_type == AIC_UART_TYPE_2) {
        ch = UART_TXDMA_CH2_IDX;
        dma_erqcsr_set(REQ_CID_UART2_TX, ch);
    } else {
        dbg("uart_type err\n");
    }

    dma_ch_rqr_erqm_setb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)UART1_TXRXD_ADDR);
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((1) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, byte_cnt);
    dma_ch_tbl2cr_set(ch, byte_cnt);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (1 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if UART_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));
    #if UART_DMA_ISR_ENABLED
    rtos_semaphore_wait(uart_wr_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    #endif

    return 0;
}

#if UART_DMA_ISR_ENABLED
void uart1_rxdma_isr(void)
{
    const int ch = UART_RXDMA_CH1_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    rtos_semaphore_signal(uart_rd_sema, true);
}

void uart1_txdma_isr(void)
{
    const int ch = UART_TXDMA_CH1_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    rtos_semaphore_signal(uart_wr_sema, true);
}

void uart2_rxdma_isr(void)
{
    const int ch = UART_RXDMA_CH2_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    rtos_semaphore_signal(uart_rd_sema, true);
}

void uart2_txdma_isr(void)
{
    const int ch = UART_TXDMA_CH2_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    rtos_semaphore_signal(uart_wr_sema, true);
}
#endif
#else  /* UART_DMA_TEST_ENABLED */
void uart_rx_handler(void)
{
    while (uart1_get_rx_count()) {
        unsigned char c = (unsigned char)uart1_getc();
        USER_PRINTF("uart rx: %02X\n", c);
    }
}
#endif /* UART_DMA_TEST_ENABLED */


