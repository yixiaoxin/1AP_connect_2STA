/**
 ****************************************************************************************
 *
 * @file uart_api.c
 *
 * @brief UART Driver APIs.
 *
 ****************************************************************************************
 */
#include "uart_api.h"
#include "uart1_api.h"
#include "uart2_api.h"
#include "reg_uart1.h"
#include "reg_uart2.h"
#include "reg_iomux.h"
#include "sysctrl_api.h"
#include "reg_ipc_comreg.h"
#include "dbg.h"

#if (PLF_HW_PXP == 1)
#define MAX_BAUD_RATE       1
#else
#define MAX_BAUD_RATE       0
#endif

/// UART Interrupt ID bits
#define UART_MODEM_INT       0x00
#define UART_NO_INT          0x01
#define UART_TX_INT          0x02
#define UART_RX_INT          0x04
#define UART_RX_ERROR_INT    0x06
#define UART_TIMEOUT_INT     0x0C
#define UART_ID_MASK         0x0F

#define PERCLK_INDEX1        (PER_UART0 + UART1_INDEX)
#define UART_INITED1_SET     M2STR_P3(ipccomreg_state_uart, UART1_INDEX, inited_setb)
#define UART_INITED1_CLR     M2STR_P3(ipccomreg_state_uart, UART1_INDEX, inited_clrb)

#define PERCLK_INDEX2        (PER_UART0 + UART2_INDEX)
#define UART_INITED2_SET     M2STR_P3(ipccomreg_state_uart, UART2_INDEX, inited_setb)
#define UART_INITED2_CLR     M2STR_P3(ipccomreg_state_uart, UART2_INDEX, inited_clrb)

struct uart_ctrl
{
    uint8_t rx_idx;
    uint8_t tx_idx;
    int uart_inited;
    uint32_t uart_baudrate;
    uart_rx_func_t rx_func;
};

static struct uart_ctrl uart1_ctrl =
{
    .rx_idx = 0,
    .tx_idx = 0,
    .uart_inited = 0,
    .uart_baudrate = 0,
    .rx_func = NULL,
};

static struct uart_ctrl uart2_ctrl =
{
    .rx_idx = 0,
    .tx_idx = 0,
    .uart_inited = 0,
    .uart_baudrate = 0,
    .rx_func = NULL,
};

static void uart1_isr_handler(void)
{
    switch (uart1_irqtyp_getf())
    {
        case UART_RX_ERROR_INT://RX Error Interrupt, highest priority
            uart1_irqsts_get();
            break;
        case UART_RX_INT://RX Data Ready Interrupt, second priority
            if (uart1_ctrl.rx_func) {
                uart1_ctrl.rx_func();
            } else {
                uart1_rxdata_getf();
            }
            break;
        case UART_TIMEOUT_INT://Char Timeout Interrupt, second priority
            uart1_rxdata_getf();
            break;
        case UART_TX_INT://TX Empty Interrupt, third priority
            break;
        case UART_MODEM_INT://Modem Interrupt, forth priority
            uart1_mdmsts_get();
            break;
        case UART_NO_INT://No Interrupt
            break;
        default:
            break;
    }
}

static void uart2_isr_handler(void)
{
    switch (uart2_irqtyp_getf())
    {
        case UART_RX_ERROR_INT://RX Error Interrupt, highest priority
            uart2_irqsts_get();
            break;
        case UART_RX_INT://RX Data Ready Interrupt, second priority
            if (uart2_ctrl.rx_func) {
                uart2_ctrl.rx_func();
            } else {
                uart2_rxdata_getf();
            }
            break;
        case UART_TIMEOUT_INT://Char Timeout Interrupt, second priority
            uart2_rxdata_getf();
            break;
        case UART_TX_INT://TX Empty Interrupt, third priority
            break;
        case UART_MODEM_INT://Modem Interrupt, forth priority
            uart2_mdmsts_get();
            break;
        case UART_NO_INT://No Interrupt
            break;
        default:
            break;
    }
}

uint32_t uart_baud_get(int uart_type)
{
    if (uart_type == UART_TYPE_1) {
        return uart1_ctrl.uart_baudrate;
    }
    else if (uart_type == UART_TYPE_2) {
        return uart2_ctrl.uart_baudrate;
    }
    else {
        dbg("uart type err\n");
        return 0;
    }
}

static void uart_baud_set(int uart_type, uint32_t baud)
{
    if (uart_type == UART_TYPE_1) {
        if (uart1_ctrl.uart_baudrate != baud) {
            uint32_t div0, div1, div2;
            #if (!MAX_BAUD_RATE)
            uint32_t clk, div;
            #endif
            uart1_ctrl.uart_baudrate = baud;
            #if (MAX_BAUD_RATE)
            div0 = 0x01UL;
            div1 = div2 = 0x00UL;
            #else
            #if (PLF_HW_FPGA == 1)
            clk = SYS_CLK_FREQUENCY_80M;
            #else
            clk = sysctrl_clock_get(PERCLK_INDEX1);
            #endif
            div  = (clk + (uart1_ctrl.uart_baudrate >> 1)) / uart1_ctrl.uart_baudrate;
            div0 = 0xFFUL & (div >> 4);
            div1 = 0xFFUL & (div >> 12);
            div2 = (0x01UL & div) + (0x07UL & (div >> 1)) + (0x70UL & (div << 3));
            #endif
            uart1_divae_setf(1);  //div reg access enable
            uart1_div0_set(div0);
            uart1_div1_set(div1);
            uart1_div2_set(div2);
            uart1_divae_setf(0);  //div reg access disable
        }
    }
    else if (uart_type == UART_TYPE_2) {
        if (uart2_ctrl.uart_baudrate != baud) {
            uint32_t div0, div1, div2;
            #if (!MAX_BAUD_RATE)
            uint32_t clk, div;
            #endif
            uart2_ctrl.uart_baudrate = baud;
            #if (MAX_BAUD_RATE)
            div0 = 0x01UL;
            div1 = div2 = 0x00UL;
            #else
            #if (PLF_HW_FPGA == 1)
            clk = SYS_CLK_FREQUENCY_80M;
            #else
            clk = sysctrl_clock_get(PERCLK_INDEX2);
            #endif
            div  = (clk + (uart2_ctrl.uart_baudrate >> 1)) / uart2_ctrl.uart_baudrate;
            div0 = 0xFFUL & (div >> 4);
            div1 = 0xFFUL & (div >> 12);
            div2 = (0x01UL & div) + (0x07UL & (div >> 1)) + (0x70UL & (div << 3));
            #endif
            uart2_divae_setf(1);  //div reg access enable
            uart2_div0_set(div0);
            uart2_div1_set(div1);
            uart2_div2_set(div2);
            uart2_divae_setf(0);  //div reg access disable
        }
    }
}

void register_uart_rx_function(int uart_type, uart_rx_func_t func)
{
    if (uart_type == UART_TYPE_1) {
        uart1_ctrl.rx_func = func;
    }
    else if (uart_type == UART_TYPE_2) {
        uart2_ctrl.rx_func = func;
    }
}

int uart_init(int uart_type, uart_cfg *cfg)
{
    switch (cfg->uart_chan) {
        case (UART1_CHANNEL_1):
            iomux_gpioa_config_sel_setf(2, 2);
            iomux_gpioa_config_sel_setf(3, 2);
            uart1_ctrl.rx_idx = 2;
            uart1_ctrl.tx_idx = 3;
            break;
        case (UART1_CHANNEL_2):
            iomux_gpioa_config_sel_setf(4, 3);
            iomux_gpioa_config_sel_setf(5, 3);
            uart1_ctrl.rx_idx = 4;
            uart1_ctrl.tx_idx = 5;
            break;
        case (UART1_CHANNEL_3):
            iomux_gpioa_config_sel_setf(10, 1);
            iomux_gpioa_config_sel_setf(11, 1);
            uart1_ctrl.rx_idx = 10;
            uart1_ctrl.tx_idx = 11;
            break;
        case (UART2_CHANNEL_1):
            iomux_gpioa_config_sel_setf(6, 2);
            iomux_gpioa_config_sel_setf(7, 2);
            uart2_ctrl.rx_idx = 6;
            uart2_ctrl.tx_idx = 7;
            break;
        case (UART2_CHANNEL_2):
            iomux_gpioa_config_sel_setf(10, 9);
            iomux_gpioa_config_sel_setf(11, 9);
            uart2_ctrl.rx_idx = 10;
            uart2_ctrl.tx_idx = 11;
            break;
        default:
            dbg("uart channel err\n");
            return -1;
            break;
    }

    if (uart_type == UART_TYPE_1) {
        cpusysctrl_hclkme_set(CPU_SYS_CTRL_HCLK_UART1_MANUAL_ENABLE);
        cpusysctrl_oclkme_set(CPU_SYS_CTRL_OTHERS_UART1_MANUAL_ENABLE);

        uart1_dbufcfg_pack(0,  //dma mode disable
                           0,  //tx data buf reset
                           0,  //rx data buf reset
                           0); //data buf disable

        uart1_irqctl_pack(0,  //
                          0,  //Timeout irq disable
                          0,  //
                          0,  //Line Status irq disable
                          0,  //Tx irq disable
                          0); //Rx irq disable

        uart1_dbufth_pack(0,  //rx timeout trigger disable
                          0,  //tx data buffer trigger threshold
                          1); //rx data buffer trigger threshold

        uart1_clk_p_setf(1);  //uart clk 0 = 24M, 1 = 48M

        uart1_format_set(8, 0, 1);

        uart_baud_set(uart_type, cfg->baudrate);

        uart1_dbufcfg_pack(0,  //dma mode disable
                           1,  //tx data buf reset
                           1,  //rx data buf reset
                           1); //data buf enable

        uart1_rxirqen_setf(1); //enable rx interrupt

        NVIC_SetVector(UART1_IRQn, (uint32_t)uart1_isr_handler);
        NVIC_SetPriority(UART1_IRQn, __NVIC_PRIO_LOWEST);
        NVIC_ClearPendingIRQ(UART1_IRQn);
        NVIC_EnableIRQ(UART1_IRQn);

        uart1_ctrl.uart_inited = 1;
        UART_INITED1_SET();

        register_uart_rx_function(uart_type, cfg->rx_func);
    }
    else if (uart_type == UART_TYPE_2) {
        cpusysctrl_hclkme_set(CPU_SYS_CTRL_HCLK_UART2_MANUAL_ENABLE);
        cpusysctrl_oclkme_set(CPU_SYS_CTRL_OTHERS_UART2_MANUAL_ENABLE);

        uart2_dbufcfg_pack(0,  //dma mode disable
                           0,  //tx data buf reset
                           0,  //rx data buf reset
                           0); //data buf disable

        uart2_irqctl_pack(0,  //
                          0,  //Timeout irq disable
                          0,  //
                          0,  //Line Status irq disable
                          0,  //Tx irq disable
                          0); //Rx irq disable

        uart2_dbufth_pack(0,  //rx timeout trigger disable
                          0,  //tx data buffer trigger threshold
                          1); //rx data buffer trigger threshold

        uart2_clk_p_setf(1);  //uart clk 0 = 24M, 1 = 48M

        uart2_format_set(8, 0, 1);

        uart_baud_set(uart_type, cfg->baudrate);

        uart2_dbufcfg_pack(0,  //dma mode disable
                           1,  //tx data buf reset
                           1,  //rx data buf reset
                           1); //data buf enable

        uart2_rxirqen_setf(1); //enable rx interrupt

        NVIC_SetVector(UART2_IRQn, (uint32_t)uart2_isr_handler);
        NVIC_SetPriority(UART2_IRQn, __NVIC_PRIO_LOWEST);
        NVIC_ClearPendingIRQ(UART2_IRQn);
        NVIC_EnableIRQ(UART2_IRQn);

        uart2_ctrl.uart_inited = 1;
        UART_INITED2_SET();

        register_uart_rx_function(uart_type, cfg->rx_func);
    }
    else {
        dbg("uart type err\n");
        return -1;
    }

    return 0;
}

void uart_deinit(int uart_type)
{
    if (uart_type == UART_TYPE_1) {
        NVIC_DisableIRQ(UART1_IRQn);
        uart1_rxirqen_setf(0); // rx int dis
        cpusysctrl_hclkmd_set(CPU_SYS_CTRL_HCLK_UART1_MANUAL_DISABLE); // clk dis
        cpusysctrl_oclkmd_set(CPU_SYS_CTRL_OTHERS_UART1_MANUAL_DISABLE);
        uart1_ctrl.uart_inited = 0;
        uart1_ctrl.uart_baudrate = 0;
        iomux_gpioa_config_sel_setf(uart1_ctrl.rx_idx, 0);
        iomux_gpioa_config_sel_setf(uart1_ctrl.tx_idx, 0);
        UART_INITED1_CLR();
    }
    else if (uart_type == UART_TYPE_2) {
        NVIC_DisableIRQ(UART2_IRQn);
        uart2_rxirqen_setf(0); // rx int dis
        cpusysctrl_hclkmd_set(CPU_SYS_CTRL_HCLK_UART2_MANUAL_DISABLE); // clk dis
        cpusysctrl_oclkmd_set(CPU_SYS_CTRL_OTHERS_UART2_MANUAL_DISABLE);
        uart2_ctrl.uart_inited = 0;
        uart2_ctrl.uart_baudrate = 0;
        iomux_gpioa_config_sel_setf(uart2_ctrl.rx_idx, 0);
        iomux_gpioa_config_sel_setf(uart2_ctrl.tx_idx, 0);
        UART_INITED2_CLR();
    }
}

void uart_recover(int uart_type)
{
    if (uart_type == UART_TYPE_1) {
        if (!uart1_rxirqen_getf()) {
            uart1_dbufth_pack(0,  //rx timeout trigger disable
                              0,  //tx data buffer trigger threshold
                              1); //rx data buffer trigger threshold
            uart1_clk_p_setf(1);  //uart clk 0 = 24M, 1 = 48M
            uart1_format_set(8, 0, 1);
            // baud rate
            {
                uint32_t div0, div1, div2;
                uint32_t clk, div;
                clk = SYS_CLK_FREQUENCY_80M;
                div  = (clk + (uart1_ctrl.uart_baudrate >> 1)) / uart1_ctrl.uart_baudrate;
                div0 = 0xFFUL & (div >> 4);
                div1 = 0xFFUL & (div >> 12);
                div2 = (0x01UL & div) + (0x07UL & (div >> 1)) + (0x70UL & (div << 3));
                uart1_divae_setf(1);  //div reg access enable
                uart1_div0_set(div0);
                uart1_div1_set(div1);
                uart1_div2_set(div2);
                uart1_divae_setf(0);  //div reg access disable
            }
            uart1_rxirqen_setf(1); //enable rx interrupt
        }
    }
    else if (uart_type == UART_TYPE_2) {
        if (!uart2_rxirqen_getf()) {
            uart2_dbufth_pack(0,  //rx timeout trigger disable
                              0,  //tx data buffer trigger threshold
                              1); //rx data buffer trigger threshold
            uart2_clk_p_setf(1);  //uart clk 0 = 24M, 1 = 48M
            uart2_format_set(8, 0, 1);
            // baud rate
            {
                uint32_t div0, div1, div2;
                uint32_t clk, div;
                clk = SYS_CLK_FREQUENCY_80M;
                div  = (clk + (uart2_ctrl.uart_baudrate >> 1)) / uart2_ctrl.uart_baudrate;
                div0 = 0xFFUL & (div >> 4);
                div1 = 0xFFUL & (div >> 12);
                div2 = (0x01UL & div) + (0x07UL & (div >> 1)) + (0x70UL & (div << 3));
                uart2_divae_setf(1);  //div reg access enable
                uart2_div0_set(div0);
                uart2_div1_set(div1);
                uart2_div2_set(div2);
                uart2_divae_setf(0);  //div reg access disable
            }
            uart2_rxirqen_setf(1); //enable rx interrupt
        }
    }
}

void uart_putc(int uart_type, char ch)
{
    if (uart_type == UART_TYPE_1) {
        while (uart1_tx_dbuf_full_getf());
        uart1_txdata_setf(ch);
    } else if (uart_type == UART_TYPE_2) {
        while (uart2_tx_dbuf_full_getf());
        uart2_txdata_setf(ch);
    } else {
        dbg("uart type err\n");
        return ;
    }
}

char uart_getc(int uart_type)
{
    if (uart_type == UART_TYPE_1) {
        while (uart1_rx_dbuf_empty_getf());
        return ((char)uart1_rxdata_getf());
    } else if (uart_type == UART_TYPE_2) {
        while (uart2_rx_dbuf_empty_getf());
        return ((char)uart2_rxdata_getf());
    } else {
        dbg("uart type err\n");
        return -1;
    }
}

int uart_get_rx_count(int uart_type)
{
    if (uart_type == UART_TYPE_1) {
        return (uart1_rx_count_getf());
    } else if (uart_type == UART_TYPE_2) {
        return (uart2_rx_count_getf());
    } else {
        dbg("uart type err\n");
        return -1;
    }
}

