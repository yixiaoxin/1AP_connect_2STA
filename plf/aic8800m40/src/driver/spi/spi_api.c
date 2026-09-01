/*
 * Copyright (C) 2018-2023 AICSemi Ltd.
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

/**
 * Includes
 */
#include "spi_api.h"
#include "gpio_api.h"
#include "dma_generic.h"
#include "sysctrl_api.h"
#include "reg_iomux.h"
#include "ll.h"
#include "dbg.h"

/* ------------------ spi api version 1 ------------------ */
/**
 * Macros
 */
#define SPI_DEBUG_PRINTF(fmt, ...)      do {} while(0)//dbg(fmt, ##__VA_ARGS__)//

/**
 * TypeDefs
 */

/**
 * Variables
 */

/**
 * Functions
 */

void spim_init(spim_cfg_t *spim)
{
    uint32_t pclk;

    // clk en
    cpusysctrl_pclkme_set(CPU_SYS_CTRL_PCLK_SPI_MANUAL_ENABLE);
    cpusysctrl_oclkme_set(CPU_SYS_CTRL_OTHERS_SPI_MANUAL_ENABLE);
    // clk freq
    pclk = sysctrl_clock_get(SYS_PCLK);
    spi_dr_div_setf(pclk / 2 / spim->clk_freq - 1);  // pclk/(2*(div+1)) = freq

    // iomux
    iomux_gpioa_config_sel_setf(10, 3); // clk
    if (spim->cs == SPI_CS0) {
        iomux_gpioa_config_sel_setf(11, 3); // cs
    }
    if ((spim->spi_mode == SPI_MODE_GENERIC) ||
        (spim->spi_mode == SPI_MODE_LCD_3W_DI_DO) ||
        (spim->spi_mode == SPI_MODE_LCD_4W_DI_DO)) {
        iomux_gpioa_config_sel_setf(12, 3); // di
    }
    iomux_gpioa_config_sel_setf(13, 3); // do

    // gpioa cs
    if ((spim->cs == SPI_CS1) && (spim->gpa_idx_cs <= GPIOA_IDX_MAX)) {
        gpioa_init(spim->gpa_idx_cs);
        gpioa_dir_out(spim->gpa_idx_cs);
        gpioa_set(spim->gpa_idx_cs);
    }
}

void spim_write_byte(spim_cfg_t *spim, uint8_t byte_val, bool cs_end)
{
    unsigned int rdata;
    int idx, rec = 0;
    if (SPI_DC_DATA == spim->dc) {
        AIC_SPI0->MR1 |=  (0x01UL << 15); // data mode
    } else if (SPI_DC_CMD == spim->dc) {
        AIC_SPI0->MR1 &= ~(0x01UL << 15); // cmd mode
    }
    AIC_SPI0->OCR = 1; // out data cnt
    AIC_SPI0->ICR = 0; // in data cnt
    if (SPI_CS0 == spim->cs) {
        AIC_SPI0->MR0   = ((0x01UL << 11) | (spim->spi_mode <<  3)); // slave, spi mode
        AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (spim->bit_cnt << 2)); // cs, bit count
        spi_cr0_clk_mode_setf(spim->clk_mode); // clock edge + phase
    } else if (SPI_CS1 == spim->cs) {
        AIC_SPI0->MR0   = ((0x00UL << 11) | (spim->spi_mode <<  3)); // slave, spi mode
        AIC_SPI0->CR[0] = ((0x0DUL <<  8) | (spim->bit_cnt << 2)); // cs, bit count
        spi_cr0_clk_mode_setf(spim->clk_mode); // clock edge + phase
        if (spim->gpa_idx_cs <= GPIOA_IDX_MAX) {
            gpioa_clr(spim->gpa_idx_cs);
        }
    }
    AIC_SPI0->CR[1] = (0x02UL << 12); // send
    AIC_SPI0->IOR   = byte_val; // tx data
    AIC_SPI0->TCR   = (0x01UL << 1); // trans start
    for (idx = 0; idx < 32; idx++) {
        __NOP(); __NOP();
        __NOP(); __NOP();
        rdata = AIC_SPI0->SR;
        if (rdata & (0x01UL << 8)) {
            rec |= (0x01UL << idx);
        } else if (idx > 4) {
            break;
        }
    }
    if (idx == 32) {
        SPI_DEBUG_PRINTF("%08x\n",rec);
        do {
            __NOP(); __NOP();
            __NOP(); __NOP();
            rdata = AIC_SPI0->SR;
        } while (rdata & (0x01UL << 8));
    }
    if (cs_end) {
        AIC_SPI0->CR[0] = (0x0FUL <<  8);
        if ((SPI_CS1 == spim->cs) && (spim->gpa_idx_cs <= GPIOA_IDX_MAX)) {
            gpioa_set(spim->gpa_idx_cs);
        }
    }
}


uint8_t spim_read_byte(spim_cfg_t *spim, bool cs_end)
{
    unsigned int rdata;
    int idx, rec = 0;
    if (SPI_DC_DATA == spim->dc) {
        AIC_SPI0->MR1 |=  (0x01UL << 15); // data mode
    } else if (SPI_DC_CMD == spim->dc) {
        AIC_SPI0->MR1 &= ~(0x01UL << 15); // cmd mode
    }
    AIC_SPI0->OCR = 1; // out data cnt
    AIC_SPI0->ICR = 1; // in data cnt
    if (SPI_CS0 == spim->cs) {
        AIC_SPI0->MR0   = ((0x01UL << 11) | (spim->spi_mode <<  3)); // slave, spi mode
        AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (spim->bit_cnt << 2)); // cs, bit count
        spi_cr0_clk_mode_setf(spim->clk_mode); // clock edge + phase
    } else if (SPI_CS1 == spim->cs) {
        AIC_SPI0->MR0   = ((0x01UL << 11) | (spim->spi_mode <<  3)); // slave, spi mode
        AIC_SPI0->CR[0] = ((0x0DUL <<  8) | (spim->bit_cnt << 2)); // cs, bit count
        spi_cr0_clk_mode_setf(spim->clk_mode); // clock edge + phase
        if (spim->gpa_idx_cs <= GPIOA_IDX_MAX) {
            gpioa_clr(spim->gpa_idx_cs);
        }
    }
    AIC_SPI0->CR[1] = (0x03UL << 12); // send and recv
    AIC_SPI0->IOR   = 0x00UL;//0xFFUL; // tx dummy data
    AIC_SPI0->TCR   = (0x01UL << 1); // trans start
    for (idx = 0; idx < 32; idx++) {
        __NOP(); __NOP();
        __NOP(); __NOP();
        rdata = AIC_SPI0->SR;
        if (rdata & (0x01UL << 8)) {
            rec |= (0x01UL << idx);
        } else if (idx > 4) {
            break;
        }
    }
    if (idx == 32) {
        SPI_DEBUG_PRINTF("%08x\n",rec);
        do {
            __NOP(); __NOP();
            __NOP(); __NOP();
            rdata = AIC_SPI0->SR;
        } while (rdata & (0x01UL << 8));
    }
    rdata = AIC_SPI0->IOR;
    if (cs_end) {
        AIC_SPI0->CR[0] = (0x0FUL << 8);
        if ((SPI_CS1 == spim->cs) && (spim->gpa_idx_cs <= GPIOA_IDX_MAX)) {
            gpioa_set(spim->gpa_idx_cs);
        }
    }
    return (uint8_t)rdata;
}


/* ------------------ spi api version 2 ------------------ */
#if SPI0_ISR_ENABLED
#include "rtos_al.h"
#endif

#define SPI0_RXDMA_CH_IDX       DMA_CHANNEL_SPI0_RX
#define SPI0_TXDMA_CH_IDX       DMA_CHANNEL_SPI0_TX
#define SPI0_RXDMA_IRQ_IDX      DMA08_IRQn
#define SPI0_TXDMA_IRQ_IDX      DMA09_IRQn

#if SPI0_ISR_ENABLED
rtos_semaphore spi0_tx_empty_sema;
rtos_semaphore spi0_rx_full_sema;
rtos_semaphore spi0_txend_sema;
rtos_semaphore spi0_rxend_sema;
void spi0_isr(void);
#endif

#if SPI0_DMA_ISR_ENABLED
rtos_semaphore spi0_rxdma_sema;
rtos_semaphore spi0_txdma_sema;
void spi0_rxdma_isr(void);
void spi0_txdma_isr(void);
#endif

/**
 ****************************************************************************************
 * @brief init spi, config iomux & clock div
 ****************************************************************************************
 */
void spi0_init(uint8_t spi_mode)
{
    uint32_t pclk;
    #if SPI0_DMA_ISR_ENABLED
    int ret;
    #endif

    // clk en
    cpusysctrl_pclkme_set(CPU_SYS_CTRL_PCLK_SPI_MANUAL_ENABLE);
    cpusysctrl_oclkme_set(CPU_SYS_CTRL_OTHERS_SPI_MANUAL_ENABLE);
    cpusysctrl_hclkme_set(CPU_SYS_CTRL_HCLK_DMA_MANUAL_ENABLE);

    pclk = sysctrl_clock_get(SYS_PCLK);
    spi_dr_div_setf(pclk / 2 / SPI_CLK_FREQ - 1);  // pclk/(2*(div+1)) = freq

    if (!spi_mode) {
        uint32_t cnt = 5;
        for (int i=0; i<cnt; i++) {
            AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
            AIC_SPI0->CR[0] = 0;
        }
    }

    // iomux
    iomux_gpioa_config_sel_setf(10, 3); // clk
    iomux_gpioa_config_sel_setf(11, 3); // cs
    iomux_gpioa_config_sel_setf(12, 3); // di
    iomux_gpioa_config_sel_setf(13, 3); // do

    #if SPI0_ISR_ENABLED
    //AIC_SPI0->IER |= (0x01UL << 8);  // txend: valid only when use DMA
    //AIC_SPI0->IER |= (0x01UL << 9);  // rxend: valid only when use DMA
    ret = rtos_semaphore_create(&spi0_tx_empty_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    ret = rtos_semaphore_create(&spi0_rx_full_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    ret = rtos_semaphore_create(&spi0_txend_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    ret = rtos_semaphore_create(&spi0_rxend_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    NVIC_SetVector(SPI_IRQn, (uint32_t)spi0_isr);
    NVIC_SetPriority(SPI_IRQn, __NVIC_PRIO_LOWEST);
    NVIC_EnableIRQ(SPI_IRQn);
    #endif

    #if SPI0_DMA_ISR_ENABLED
    ret = rtos_semaphore_create(&spi0_txdma_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    ret = rtos_semaphore_create(&spi0_rxdma_sema, 1, 0);
    if (ret) {
        dbg("sema create fail: %d\r\n", ret);
    }
    NVIC_SetVector(SPI0_RXDMA_IRQ_IDX, (uint32_t)spi0_rxdma_isr);
    NVIC_SetPriority(SPI0_RXDMA_IRQ_IDX, __NVIC_PRIO_LOWEST);
    NVIC_EnableIRQ(SPI0_RXDMA_IRQ_IDX);
    NVIC_SetVector(SPI0_TXDMA_IRQ_IDX, (uint32_t)spi0_txdma_isr);
    NVIC_SetPriority(SPI0_TXDMA_IRQ_IDX, __NVIC_PRIO_LOWEST);
    NVIC_EnableIRQ(SPI0_TXDMA_IRQ_IDX);
    #endif
}

void spi0_deinit(void)
{
    // iomux
    iomux_gpioa_config_sel_setf(10, 0); // clk
    iomux_gpioa_config_sel_setf(11, 0); // cs
    iomux_gpioa_config_sel_setf(12, 0); // di
    iomux_gpioa_config_sel_setf(13, 0); // do

    // clk rst
    cpusysctrl_pclkrs_set(CPU_SYS_CTRL_PRESETN_SPI_SET);
    cpusysctrl_pclkrc_set(CPU_SYS_CTRL_PRESETN_SPI_CLEAR);
    cpusysctrl_oclkrs_set(CPU_SYS_CTRL_RESETN_SPI_SET);
    cpusysctrl_oclkrc_set(CPU_SYS_CTRL_RESETN_SPI_CLEAR);

    #if SPI0_DMA_ISR_ENABLED
    // dma disable
    dma_ch_icsr_set(SPI0_RXDMA_CH_IDX, (dma_ch_icsr_get(SPI0_RXDMA_CH_IDX) | DMA_CH_TBL2_ICLR_BIT));
    dma_ch_ctlr_set(SPI0_RXDMA_CH_IDX, 0);
    dma_ch_icsr_set(SPI0_TXDMA_CH_IDX, (dma_ch_icsr_get(SPI0_TXDMA_CH_IDX) | DMA_CH_TBL2_ICLR_BIT));
    dma_ch_ctlr_set(SPI0_TXDMA_CH_IDX, 0);
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_delete(spi0_tx_empty_sema);
    rtos_semaphore_delete(spi0_rx_full_sema);
    rtos_semaphore_delete(spi0_txend_sema);
    rtos_semaphore_delete(spi0_rxend_sema);
    NVIC_DisableIRQ(SPI_IRQn);
    #endif

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_delete(spi0_txdma_sema);
    rtos_semaphore_delete(spi0_rxdma_sema);
    NVIC_DisableIRQ(SPI0_RXDMA_IRQ_IDX);
    NVIC_DisableIRQ(SPI0_TXDMA_IRQ_IDX);
    #endif
}

/**
 ****************************************************************************************
 * @brief spi write and read word, slave mode, don`t use DMA.
 * @Support: AIC8800MC/AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_write_read_words(unsigned int *buff_wr, unsigned int word_cnt_wr, unsigned int *buff_rd, unsigned int word_cnt_rd)
{
    if (word_cnt_wr > 31 || word_cnt_rd > 31) {
        dbg("spi-fifo len is 31.\n");
        return -1;
    }

    AIC_SPI0->CR[3] = (((word_cnt_rd)&31)<<0) | (((0)&31)<<8);
    AIC_SPI0->OFCR = (((word_cnt_wr)&31)<<0) | (((0)&31)<<8);
    AIC_SPI0->MR0 = (((0)&7)<<3) | (1<<11) | (1<<10) | (1<<0);
    AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);                          // transbit 32
    AIC_SPI0->CR[1] = (1<<6) | (((0x3)&3)<<12);                         // recv and send trans
    AIC_SPI0->CR[5] = (((0x100)&0xFFFF)<<0);
    AIC_SPI0->CR[2] |= (1<<5);
    AIC_SPI0->TCR = (1<<1);                                             // rx req start

    for (int i=0; i<word_cnt_wr; i++) {                                 // send data
        AIC_SPI0->IOR = buff_wr[i];
    }

    for (int i=0; i<word_cnt_rd; i++) {                                 // read data (block-mode)
        while ((AIC_SPI0->SR & (1<<5)) == (1<<5)) ;
        buff_rd[i] = AIC_SPI0->IOR;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write word, slave mode, don`t use DMA.
 * @Support: AIC8800MC/AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_write_words(unsigned int *buff_wr, unsigned int word_cnt_wr)
{
    if (word_cnt_wr > 31) {
        dbg("spi-fifo len is 31.\n");
        return -1;
    }

    AIC_SPI0->OFCR = (((word_cnt_wr)&31)<<0) | (((0)&31)<<8);
    AIC_SPI0->MR0 = (((0)&7)<<3) | (1<<11) | (1<<10) | (1<<0);
    AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);                         // transbit 32
    AIC_SPI0->CR[1] = (1<<6) | (((0x2)&3)<<12);                        // send trans
    AIC_SPI0->CR[5] = (((0x100)&0xFFFF)<<0);
    AIC_SPI0->CR[2] |= (1<<5);
    AIC_SPI0->TCR = (1<<1);                                            // tx req start

    for (int i=0; i<word_cnt_wr; i++) {
        AIC_SPI0->IOR = buff_wr[i];
    }

    AIC_SPI0->IER |= (1<<7);                                           // Tx fifo empty interrupt enable
    //dbg("AIC_SPI0->IER: %08x\n", AIC_SPI0->IER);

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_tx_empty_sema, -1);
    #endif

    AIC_SPI0->CR[0] = (0x0FUL << 8);                                   // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi read word, slave mode, don`t use DMA.
 * @Support: AIC8800MC/AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_read_words(unsigned int *buff_rd, unsigned int word_cnt_rd)
{
    if (word_cnt_rd > 31) {
        dbg("spi-fifo len is 31.\n");
        return -1;
    }

    AIC_SPI0->CR[3] = (((word_cnt_rd)&31)<<0) | (((0)&31)<<8);
    AIC_SPI0->MR0 = (((0)&7)<<3) | (1<<11) | (1<<10) | (1<<0);
    AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);                         // transbit 32
    AIC_SPI0->CR[1] = (1<<6) | (((0x1)&3)<<12);                        // recv trans
    AIC_SPI0->CR[5] = (((0x100)&0xFFFF)<<0);
    AIC_SPI0->CR[2] |= (1<<5);
    AIC_SPI0->TCR = (1<<0);                                            // rx req start

    AIC_SPI0->IER |= (1<<6);                                           // Rx fifo full interrupt enable
    //dbg("AIC_SPI0->IER: %08x\n", AIC_SPI0->IER);

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rx_full_sema, -1);
    #endif

    for (int i=0; i<word_cnt_rd; i++) {
        //while ((AIC_SPI0->SR & (1<<5)) == (1<<5)) ; // block
        buff_rd[i] = AIC_SPI0->IOR;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write and read, master and slave mode, use DMA.
 * @Support: AIC8800M40
 * @param: mode       1:MASTER, 0:SLAVE
 *         bit_mode   0:8bit, 1:16bit, 2:24bit, 3:32bit
 ****************************************************************************************
 */
int spi0_dma_write_read(unsigned int *buff_wr, unsigned int word_cnt_wr,
                              unsigned int *buff_rd, unsigned int word_cnt_rd, int mode, int bit_mode)
{
    if(!mode){
        uint32_t cnt = 5;
        for (int i = 0; i<cnt; i++) {
            AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
            AIC_SPI0->CR[0] = 0;
        }
    }

    int ch;
    unsigned int bit_len, shift_len;
    AIC_SPI0->CR[1] = 0x00UL;

    bit_len = (bit_mode == 3) ? 32 : (bit_mode == 2) ? 24 : (bit_mode == 1) ? 16 : 8;
    shift_len= (bit_mode == 3) ? 2 : bit_mode;

    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);

    if ((bit_len == 32) || (bit_len == 24)){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    } else if (bit_len == 16){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    } else if (bit_len == 8){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    }

    dma_ch_tbl1cr_set(ch, word_cnt_wr<<shift_len);
    dma_ch_tbl2cr_set(ch, word_cnt_wr<<shift_len);
    dma_ch_tsr_set(ch, (((1<<shift_len) << DMA_CH_STRANSZ_LSB) | ((1<<shift_len) << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    if ((bit_len == 32) || (bit_len == 24)){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    } else if (bit_len == 16){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    } else if (bit_len == 8){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    }

    dma_ch_tbl1cr_set(ch, word_cnt_rd<<shift_len);
    dma_ch_tbl2cr_set(ch, word_cnt_rd<<shift_len);
    dma_ch_tsr_set(ch, (((1<<shift_len) << DMA_CH_STRANSZ_LSB) | ((1<<shift_len) << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8) | (0x01UL << 9);
    #endif

    if (mode) { // master mode
        AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
        AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = ((0x02UL << 8) | (0x01UL << 0));
        AIC_SPI0->CR[1] = ((0x03UL << 12) | (0x01UL << 6)); // send and recv, cs, 3-wire
        AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (0x01UL << 1) | (((bit_len)&31) << 2) ); // cs, bit count
        AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL << 3)); // slave, generic mode
        AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start
        AIC_SPI0->CR[5] = (((0x20)&0xFFFF)<<0);
    }
    else { // slave mode
        AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
        AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
        AIC_SPI0->CR[0] = (((bit_len)&31)<<2) | (1<<1);
        AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
        AIC_SPI0->CR[2] = (0x01UL << 5);
        AIC_SPI0->CR[1] = (((0x3UL)&3)<<12) | (0x01UL <<  6); // send and recv, cs, 3-wire
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start
        AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma
    }

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxend_sema, -1);
    #endif

    while(AIC_SPI0->RESERVED2 == 0);
    //dbg("AIC_SPI0->RESERVED2: %x\n", AIC_SPI0->RESERVED2);

    if (mode) {
        AIC_SPI0->CR[2] = 0;
    } else {
        AIC_SPI0->CR[2] = (0x01UL << 5);
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write, slave mode, use DMA.
 * @Support: AIC8800M40
 * @param: bit_mode   0:8bit, 1:16bit, 2:24bit, 3:32bit
 ****************************************************************************************
 */
int spi0_slave_dma_write(unsigned int *buff_wr, unsigned int word_cnt_wr, int bit_mode)
{
    uint32_t cnt = 5;
    for (int i=0; i<cnt; i++) {
        AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
        AIC_SPI0->CR[0] = 0;
    }

    unsigned int bit_len, shift_len;
    bit_len = (bit_mode == 3) ? 32 : (bit_mode == 2) ? 24 : (bit_mode == 1) ? 16 : 8;
    shift_len= (bit_mode == 3) ? 2: bit_mode;

    int ch;
    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    if ((bit_len == 32) || (bit_len == 24)){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    } else if (bit_len == 16){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    } else if (bit_len == 8){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    }
    //dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
    //                         (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_wr<<shift_len);
    dma_ch_tbl2cr_set(ch, word_cnt_wr<<shift_len);
    dma_ch_tsr_set(ch, (((1<<shift_len) << DMA_CH_STRANSZ_LSB) | ((1<<shift_len) << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8);  // txend: valid only when use DMA
    #endif

    AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
    AIC_SPI0->OFCR  = (((30)&31)<<8);
    AIC_SPI0->CR[0] = (((bit_len)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x2UL)&3)<<12) | (0x01UL <<  6);        // send trans
    AIC_SPI0->TCR   = (0x01UL << 1);                             // tx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma

    uint32_t reg = 0xFFFFFFFF;
    while((reg & (1<<7)) == (1<<7)) {
        reg = AIC_SPI0->SR;
    }

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_txdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_txend_sema, -1);
    #endif

    reg = 0;
    while((reg & (1<<0)) != (1<<0)) {
        reg = AIC_SPI0->RESERVED2;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

/**
 ****************************************************************************************
 * @brief spi read, slave mode, use DMA.
 * @Support: AIC8800M40
 * @param: bit_mode   0:8bit, 1:16bit, 2:24bit, 3:32bit
 ****************************************************************************************
 */
int spi0_slave_dma_read(unsigned int *buff_rd, unsigned int word_cnt_rd,int bit_mode)
{
    unsigned int bit_len, shift_len;
    bit_len = (bit_mode == 3) ? 32 : (bit_mode == 2) ? 24 : (bit_mode == 1) ? 16 : 8;
    shift_len = (bit_mode == 3) ? 2 : bit_mode;

    int ch;
    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    if ((bit_len == 32) || (bit_len == 24)){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    } else if (bit_len == 16){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    } else if (bit_len == 8){
          dma_ch_tbl0cr_set(ch, ((1<<shift_len) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    }
    //dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
    //                         (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_rd<<shift_len);
    dma_ch_tbl2cr_set(ch, word_cnt_rd<<shift_len);
    dma_ch_tsr_set(ch, (((1<<shift_len) << DMA_CH_STRANSZ_LSB) | ((1<<shift_len) << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 9);  // rxend: valid only when use DMA
    #endif

    AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
    AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
    AIC_SPI0->CR[0] = (((bit_len)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x1UL)&3)<<12) | (0x01UL <<  6);        // recv trans
    AIC_SPI0->TCR   = (0x01UL << 0);                             // rx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxend_sema, -1);
    #endif

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    uint32_t reg = 0;
    while((reg & (1<<0)) != (1<<0)) {
        reg = AIC_SPI0->RESERVED2;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write and read word, master and slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
int spi0_dma_write_read_words(unsigned int *buff_wr, unsigned int word_cnt_wr,
                                       unsigned int *buff_rd, unsigned int word_cnt_rd, int mode)
{
    if(!mode){
        uint32_t cnt = 5;
        for (int i=0; i<cnt; i++) {
            AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
            AIC_SPI0->CR[0] = 0;
        }
    }

    int ch;
    AIC_SPI0->CR[1] = 0x00UL;

    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_wr<<2);
    dma_ch_tbl2cr_set(ch, word_cnt_wr<<2);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_rd<<2);
    dma_ch_tbl2cr_set(ch, word_cnt_rd<<2);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8) | (0x01UL << 9);
    #endif

    if (mode) { // master mode
        AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
        AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = ((0x02UL << 8) | (0x01UL << 0));
        AIC_SPI0->CR[1] = ((0x03UL << 12) | (0x01UL << 6)); // send and recv, cs, 3-wire
        AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (0x01UL << 1) | (((32)&31) << 2) ); // cs, bit count
        AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL << 3)); // slave, generic mode
        AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start
        AIC_SPI0->CR[5] = (((0x20)&0xFFFF)<<0);
    }
    else { // slave mode
        AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
        AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
        AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);
        AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
        AIC_SPI0->CR[2] = (0x01UL << 5);
        AIC_SPI0->CR[1] = (((0x3UL)&3)<<12) | (0x01UL <<  6); // send and recv, cs, 3-wire
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start
        AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma
    }

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxend_sema, -1);
    #endif

    while(AIC_SPI0->RESERVED2 == 0);
    //dbg("AIC_SPI0->RESERVED2: %x\n", AIC_SPI0->RESERVED2);

    if (mode) {
        AIC_SPI0->CR[2] = 0;
    } else {
        AIC_SPI0->CR[2] = (0x01UL << 5);
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write word, slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_dma_write_words(unsigned int *buff_wr, unsigned int word_cnt_wr)
{
    uint32_t cnt = 5;
    for (int i=0; i<cnt; i++) {
        AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
        AIC_SPI0->CR[0] = 0;
    }

    int ch;
    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_wr<<2);
    dma_ch_tbl2cr_set(ch, word_cnt_wr<<2);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8);  // txend: valid only when use DMA
    #endif

    AIC_SPI0->OCR   = word_cnt_wr; // out data cnt
    AIC_SPI0->OFCR  = (((30)&31)<<8);
    AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x2UL)&3)<<12) | (0x01UL <<  6);        // send trans
    AIC_SPI0->TCR   = (0x01UL << 1);                             // tx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma

    uint32_t reg = 0xFFFFFFFF;
    while((reg & (1<<7)) == (1<<7)) {
        reg = AIC_SPI0->SR;
    }

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_txdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_txend_sema, -1);
    #endif

    reg = 0;
    while((reg & (1<<0)) != (1<<0)) {
        reg = AIC_SPI0->RESERVED2;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

/**
 ****************************************************************************************
 * @brief spi read word, slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_dma_read_words(unsigned int *buff_rd, unsigned int word_cnt_rd)
{
    int ch;
    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, word_cnt_rd<<2);
    dma_ch_tbl2cr_set(ch, word_cnt_rd<<2);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 9);  // rxend: valid only when use DMA
    #endif

    AIC_SPI0->ICR   = word_cnt_rd; // in data cnt
    AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
    AIC_SPI0->CR[0] = (((32)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x1UL)&3)<<12) | (0x01UL <<  6);        // recv trans
    AIC_SPI0->TCR   = (0x01UL << 0);                             // rx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma


    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxend_sema, -1);
    #endif

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    uint32_t reg = 0;
    while((reg & (1<<0)) != (1<<0)) {
        reg = AIC_SPI0->RESERVED2;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/* Abort only the active SPI0 DMA transaction.  spi0_deinit()/spi0_init() may
 * be called by the application afterwards for a complete peripheral recovery.
 */
static void spi0_dma_abort_current(void)
{
    AIC_SPI0->IER = 0;
    AIC_SPI0->CR[1] = 0;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR = 0;
    AIC_SPI0->CR[0] = (0x0FUL << 8);

    dma_ch_ctlr_set(SPI0_RXDMA_CH_IDX, 0);
    dma_ch_ctlr_set(SPI0_TXDMA_CH_IDX, 0);
    dma_ch_icsr_set(SPI0_RXDMA_CH_IDX,
                    dma_ch_icsr_get(SPI0_RXDMA_CH_IDX) | DMA_CH_TBL2_ICLR_BIT);
    dma_ch_icsr_set(SPI0_TXDMA_CH_IDX,
                    dma_ch_icsr_get(SPI0_TXDMA_CH_IDX) | DMA_CH_TBL2_ICLR_BIT);
}

static int spi0_wait_reserved2_nonzero(int timeout_ms)
{
    uint32_t spins = (timeout_ms < 0) ? 0xFFFFFFFFUL :
                     ((uint32_t)timeout_ms + 1U) * 20000U;

    while (AIC_SPI0->RESERVED2 == 0U) {
        if (spins != 0xFFFFFFFFUL) {
            if (spins == 0U) {
                return SPI0_DMA_ERR_TIMEOUT;
            }
            spins--;
        }
    }
    return 0;
}

static int spi0_wait_sr_bit_clear(uint32_t mask, int timeout_ms)
{
    uint32_t spins = (timeout_ms < 0) ? 0xFFFFFFFFUL :
                     ((uint32_t)timeout_ms + 1U) * 20000U;

    while ((AIC_SPI0->SR & mask) != 0U) {
        if (spins != 0xFFFFFFFFUL) {
            if (spins == 0U) {
                return SPI0_DMA_ERR_TIMEOUT;
            }
            spins--;
        }
    }
    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write and read 16bit, master and slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
static int spi0_dma_write_read_16bit_internal(unsigned int *buff_wr, unsigned int wr_len,
                                       unsigned int *buff_rd, unsigned int rd_len, int mode,
                                       int timeout_ms)
{
#if SPI0_DMA_ISR_ENABLED
    while (rtos_semaphore_wait(spi0_rxdma_sema, 0) == 0) {}
    while (rtos_semaphore_wait(spi0_txdma_sema, 0) == 0) {}
#endif
#if SPI0_ISR_ENABLED
    while (rtos_semaphore_wait(spi0_rxend_sema, 0) == 0) {}
    while (rtos_semaphore_wait(spi0_txend_sema, 0) == 0) {}
#endif

    if(!mode){
        uint32_t cnt = 5;
        for (int i=0; i<cnt; i++) {
            AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
            AIC_SPI0->CR[0] = 0;
        }
    }

    int ch;
    AIC_SPI0->CR[1] = 0x00UL;

    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((2) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, wr_len<<1);
    dma_ch_tbl2cr_set(ch, wr_len<<1);
    dma_ch_tsr_set(ch, ((2 << DMA_CH_STRANSZ_LSB) | (2 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_tbl0cr_set(ch, ((2) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, rd_len<<1);
    dma_ch_tbl2cr_set(ch, rd_len<<1);
    dma_ch_tsr_set(ch, ((2 << DMA_CH_STRANSZ_LSB) | (2 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8) | (0x01UL << 9);
    #endif

    if (mode) { // master mode
        AIC_SPI0->OCR   = wr_len; // out data cnt
        AIC_SPI0->ICR   = rd_len; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = ((0x02UL << 8) | (0x01UL << 0));
        AIC_SPI0->CR[1] = ((0x03UL << 12) | (0x01UL << 6)); // send and recv, cs, 3-wire
        AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (0x01UL << 1) | (((16)&31) << 2) ); // cs, bit count
        AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL << 3)); // slave, generic mode
        AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start
        AIC_SPI0->CR[5] = (((0x20)&0xFFFF)<<0);
    }
    else { // slave mode
        AIC_SPI0->OCR   = wr_len; // out data cnt
        AIC_SPI0->ICR   = rd_len; // in data cnt
        AIC_SPI0->OFCR  = (((30)&31)<<8);
        AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
        AIC_SPI0->CR[0] = (((16)&31)<<2) | (1<<1);
        AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
        AIC_SPI0->CR[2] = (0x01UL << 5);
        AIC_SPI0->CR[1] = (((0x3UL)&3)<<12) | (0x01UL <<  6); // send and recv, cs, 3-wire
        AIC_SPI0->TCR   = (0x01UL << 1); // trans start

        AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma
    }

    #if SPI0_DMA_ISR_ENABLED
    if (rtos_semaphore_wait(spi0_rxdma_sema, timeout_ms)) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    if (rtos_semaphore_wait(spi0_rxend_sema, timeout_ms)) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }
    #endif

    if (spi0_wait_reserved2_nonzero(timeout_ms) != 0) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }
    //dbg("AIC_SPI0->RESERVED2: %x\n", AIC_SPI0->RESERVED2);

    if (mode) {
        AIC_SPI0->CR[2] = 0;
    } else {
        AIC_SPI0->CR[2] = (0x01UL << 5);
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

int spi0_dma_write_read_16bit_timeout(unsigned int *buff_wr, unsigned int wr_len,
                                       unsigned int *buff_rd, unsigned int rd_len,
                                       int mode, int timeout_ms)
{
    return spi0_dma_write_read_16bit_internal(buff_wr, wr_len, buff_rd, rd_len,
                                               mode, timeout_ms);
}

int spi0_dma_write_read_16bit(unsigned int *buff_wr, unsigned int wr_len,
                              unsigned int *buff_rd, unsigned int rd_len, int mode)
{
    return spi0_dma_write_read_16bit_internal(buff_wr, wr_len, buff_rd, rd_len, mode, -1);
}

/**
 ****************************************************************************************
 * @brief spi write 16bit, slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
static int spi0_slave_dma_write_16bit_internal(unsigned int *buff_wr, unsigned int wr_len,
                                                  int timeout_ms)
{
#if SPI0_DMA_ISR_ENABLED
    while (rtos_semaphore_wait(spi0_txdma_sema, 0) == 0) {}
#endif
#if SPI0_ISR_ENABLED
    while (rtos_semaphore_wait(spi0_txend_sema, 0) == 0) {}
#endif

    uint32_t cnt = 5;
    for (int i=0; i<cnt; i++) {
        AIC_SPI0->CR[0] = ((1<<1) | (1<<0));
        AIC_SPI0->CR[0] = 0;
    }

    int ch;
    // dma channal for spi_tx
    ch = SPI0_TXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((2) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, wr_len<<1);
    dma_ch_tbl2cr_set(ch, wr_len<<1);
    dma_ch_tsr_set(ch, ((2 << DMA_CH_STRANSZ_LSB) | (2 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 8);  // txend: valid only when use DMA
    #endif

    AIC_SPI0->OCR   = wr_len; // out data cnt
    AIC_SPI0->ICR   = wr_len; // out data cnt
    AIC_SPI0->OFCR  = (((30)&31)<<8);
    AIC_SPI0->CR[0] = (((16)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x2UL)&3)<<12) | (0x01UL <<  6);        // send trans
    AIC_SPI0->TCR   = (0x01UL << 1);                             // tx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma

    if (spi0_wait_sr_bit_clear((1UL << 7), timeout_ms) != 0) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }

    #if SPI0_DMA_ISR_ENABLED
    if (rtos_semaphore_wait(spi0_txdma_sema, timeout_ms)) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    if (rtos_semaphore_wait(spi0_txend_sema, timeout_ms)) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }
    #endif

    if (spi0_wait_reserved2_nonzero(timeout_ms) != 0) {
        spi0_dma_abort_current();
        return SPI0_DMA_ERR_TIMEOUT;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    AIC_SPI0->MR0   |=(1<<11);
    cpusysctrl_oclkrs_set(0x800);
    cpusysctrl_oclkrc_set(0x800);
    AIC_SPI0->MR0   &=~(1<<11);
    return 0;
}

int spi0_slave_dma_write_16bit_timeout(unsigned int *buff_wr, unsigned int wr_len,
                                       int timeout_ms)
{
    return spi0_slave_dma_write_16bit_internal(buff_wr, wr_len, timeout_ms);
}

int spi0_slave_dma_write_16bit(unsigned int *buff_wr, unsigned int wr_len)
{
    return spi0_slave_dma_write_16bit_internal(buff_wr, wr_len, -1);
}

/**
 ****************************************************************************************
 * @brief spi read 16bit, slave mode, use DMA.
 * @Support: AIC8800M40
 ****************************************************************************************
 */
int spi0_slave_dma_read_16bit(unsigned int *buff_rd, unsigned int data_len)
{
    int ch;

    // dma channal for spi_rx
    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_tbl0cr_set(ch, ((2) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_HWORD << DMA_CH_DBUSU_LSB) |
                             (AHB_HWORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, data_len<<1);
    dma_ch_tbl2cr_set(ch, data_len<<1);
    dma_ch_tsr_set(ch, ((2 << DMA_CH_STRANSZ_LSB) | (2 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    #if SPI0_ISR_ENABLED
    AIC_SPI0->IER = (0x01UL << 9);  // rxend: valid only when use DMA
    #endif

    AIC_SPI0->ICR   = data_len; // in data cnt
    AIC_SPI0->OCR   = data_len; // in data cnt
    AIC_SPI0->CR[3] = (((0x1UL)&31)<<0) | (((0x2UL)&31)<<8);
    AIC_SPI0->CR[0] = (((16)&31)<<2) | (1<<1);                   // transbit 32
    AIC_SPI0->MR0   = (((0x0)&7)<<3) | (0x01UL << 10)  | (1<<0);
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->CR[1] = (((0x1UL)&3)<<12) | (0x01UL <<  6);        // recv trans
    AIC_SPI0->TCR   = (0x01UL << 0);                             // rx req start
    AIC_SPI0->CR[2] |= (0x01UL <<  6); // en dma

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxend_sema, -1);
    #endif

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    #endif

    uint32_t reg = 0;
    while((reg & (1<<0)) != (1<<0)) {
        reg = AIC_SPI0->RESERVED2;
    }

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[1] = 0x00UL;
    AIC_SPI0->CR[2] = (0x01UL << 5);
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi read 8bit, master mode, use DMA.
 * @Support: AIC8800M40/AIC8800MC
 ****************************************************************************************
 */
int spi0_dma_read_bytes(unsigned char *buff_rd, unsigned int byte_cnt_rd)
{
    int ch = SPI0_RXDMA_CH_IDX;
    byte_cnt_rd = MIN(byte_cnt_rd, SPI0_DMA_BYTE_CNT_MAX);
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)buff_rd);
    dma_ch_sar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_tbl0cr_set(ch, ((1) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, byte_cnt_rd);
    dma_ch_tbl2cr_set(ch, byte_cnt_rd);
    dma_ch_tsr_set(ch, ((1 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    AIC_SPI0->OCR   = byte_cnt_rd; // out data cnt
    AIC_SPI0->ICR   = byte_cnt_rd; // in data cnt
    AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL <<  3)); // slave, generic mode
    AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (8 << 2)); // cs, bit count
    spi_cr0_clk_mode_setf(SPI_CR_CLK_MODE_0); // clock edge + phase
    AIC_SPI0->CR[1] = ((0x01UL << 12) | (0x01UL <<  6)
        #if SPI0_3_WIRE_ENABLED
        | (0x01UL << 5)
        #endif
        ); // recv, cs, 3-wire
    AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
    AIC_SPI0->CR[3] = ((0x02UL << 8) | (0x01UL << 0));
    AIC_SPI0->TCR   = (0x01UL << 1); // trans start

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_rxdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    #endif

    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write 8bit, master mode, use DMA.
 * @Support: AIC8800M40/AIC8800MC
 ****************************************************************************************
 */
int spi0_dma_write_bytes(unsigned char *buff_wr, unsigned int byte_cnt)
{
    const int ch = SPI0_TXDMA_CH_IDX;
    byte_cnt = MIN(byte_cnt, SPI0_DMA_BYTE_CNT_MAX);

    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
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
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    AIC_SPI0->OCR   = byte_cnt; // out data cnt
    AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL <<  3)); // slave, generic mode
    AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (8 << 2)); // cs, bit count
    spi_cr0_clk_mode_setf(SPI_CR_CLK_MODE_0); // clock edge + phase
    AIC_SPI0->CR[1] = ((0x02UL << 12) | (0x01UL <<  6)
        #if SPI0_3_WIRE_ENABLED
        | (0x01UL << 5)
        #endif
        ); // send, cs, 3-wire
    AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
    AIC_SPI0->TCR   = (0x01UL << 1); // trans start

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_txdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_txend_sema, -1);
    #else
    while (AIC_SPI0->SR & (0x01UL << 8)); // busy
    #endif
    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief spi write 32bit, master mode, use DMA.
 * @Support: AIC8800M40/AIC8800MC
 ****************************************************************************************
 */
int spi0_dma_write_words(unsigned int *buff_wr, unsigned int word_cnt)
{
    const int ch = SPI0_TXDMA_CH_IDX;
    unsigned int byte_cnt = MIN(word_cnt * 4, SPI0_DMA_BYTE_CNT_MAX);

    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&AIC_SPI0->IOR));
    dma_ch_sar_set(ch, (unsigned int)buff_wr);
    dma_ch_tbl0cr_set(ch, ((4) | (REQ_TBL0 << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, byte_cnt);
    dma_ch_tbl2cr_set(ch, byte_cnt);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    #if SPI0_DMA_ISR_ENABLED
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    #endif
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    AIC_SPI0->OCR   = word_cnt; // out data cnt
    AIC_SPI0->MR0   = ((0x01UL << 11) | (0x00UL <<  3)); // slave, generic mode
    AIC_SPI0->CR[0] = ((0x0EUL <<  8) | (0 << 2)); // cs, bit count
    spi_cr0_clk_mode_setf(SPI_CR_CLK_MODE_0); // clock edge + phase
    AIC_SPI0->CR[1] = ((0x02UL << 12) | (0x01UL <<  6)
        #if SPI0_3_WIRE_ENABLED
        | (0x01UL << 5)
        #endif
        ); // send, cs, 3-wire
    AIC_SPI0->CR[2] = (0x01UL <<  6); // en dma
    AIC_SPI0->TCR   = (0x01UL << 1); // trans start

    #if SPI0_DMA_ISR_ENABLED
    rtos_semaphore_wait(spi0_txdma_sema, -1);
    #else
    while (dma_ch_icsr_tbl2_irst_getb(ch) == 0);
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    #endif

    #if SPI0_ISR_ENABLED
    rtos_semaphore_wait(spi0_txend_sema, -1);
    #else
    while (AIC_SPI0->SR & (0x01UL << 8)); // busy
    #endif
    AIC_SPI0->CR[0] = (0x0FUL << 8); // cs
    AIC_SPI0->CR[2] = 0x00UL;
    AIC_SPI0->TCR   = 0x00UL;

    return 0;
}

#if SPI0_ISR_ENABLED
void spi0_isr(void)
{
    uint32_t reg;
    uint32_t int_st = AIC_SPI0->IMSR;
    //dbg("spi0_isr: %08x\n", int_st);
    AIC_SPI0->ICLR = AIC_SPI0->IRSR;  // int clr
    if (int_st & (0x01UL<<6)) {                       // Rx fifo full interrupt
        //AIC_SPI0->ICLR |= (0x01UL << 0); // int clr
        reg = AIC_SPI0->IER;
        AIC_SPI0->IER = reg & (~(0x01UL << 6));         // disable int
        rtos_semaphore_signal(spi0_rx_full_sema, true);
    }
    if (int_st & (0x01UL<<7)) {                  // Tx fifo empty interrupt
        //AIC_SPI0->ICLR |= (0x01UL << 3); // int clr
        reg = AIC_SPI0->IER;
        AIC_SPI0->IER = reg & (~(0x01UL << 7));         // disable int
        rtos_semaphore_signal(spi0_tx_empty_sema, true);
    }
    if (int_st & (0x01UL<<8)) {                  // txend interrupt
        rtos_semaphore_signal(spi0_txend_sema, true);
    }
    if (int_st & (0x01UL<<9)) {                  // rxend interrupt
        rtos_semaphore_signal(spi0_rxend_sema, true);
    }
}
#endif

#if SPI0_DMA_ISR_ENABLED
void spi0_rxdma_isr(void)
{
    const int ch = SPI0_RXDMA_CH_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    dma_ch_ctlr_set(ch, 0);
    rtos_semaphore_signal(spi0_rxdma_sema, true);
}

void spi0_txdma_isr(void)
{
    const int ch = SPI0_TXDMA_CH_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT));
    dma_ch_ctlr_set(ch, 0);
    rtos_semaphore_signal(spi0_txdma_sema, true);
}
#endif

