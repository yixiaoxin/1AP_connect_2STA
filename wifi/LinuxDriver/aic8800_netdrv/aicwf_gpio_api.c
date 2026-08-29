/**
 * aicwf_gpio_api.c
 *
 * rftest cmd declarations
 *
 * Copyright (C) AICSemi 2018-2025
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <linux/stddef.h>
#include "aicwf_reg_def.h"
#include "rwnx_msg_tx.h"
#include "rwnx_defs.h"
#include "lmac_msg.h"

AIC_GPIO_TypeDef AIC_GPIO;
AIC_IOMUX_TypeDef AIC_IOMUX;
AIC_CPUSYSCTRL_TypeDef AIC_CPUSYSCTRL;

//#define AICRFTESTDBG(args, arg...)	do { } while (0)
#define AICRFTESTDBG(args, arg...)	do { printk(args, ##arg); } while (0)

static void iomux_gpioa_config_sel_setf(struct rwnx_hw *rwnx_hw, int gp_idx, uint8_t sel_val)
{
    //uint32_t local_val = AIC_IOMUX->pad_gpioa_cfg_reg[gp_idx] & ~IOMUX_PAD_GPIOA_0_SEL_MASK;
    //AIC_IOMUX->pad_gpioa_cfg_reg[gp_idx] = local_val | ((sel_val << IOMUX_PAD_GPIOA_0_SEL_LSB) & IOMUX_PAD_GPIOA_0_SEL_MASK);

    int ret = 0;
    uint32_t local_val = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;

    read_addr = AIC_IOMUX_BASE + offsetof(AIC_IOMUX_TypeDef, pad_gpioa_cfg_reg[gp_idx]);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    local_val = mem_read_cfm.memdata & ~IOMUX_PAD_GPIOA_0_SEL_MASK;
    write_val = local_val | ((sel_val << IOMUX_PAD_GPIOA_0_SEL_LSB) & IOMUX_PAD_GPIOA_0_SEL_MASK);
    write_addr = AIC_IOMUX_BASE + offsetof(AIC_IOMUX_TypeDef, pad_gpioa_cfg_reg[gp_idx]);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

static void iomux_gpiob_config_sel_setf(struct rwnx_hw *rwnx_hw, int gp_idx, uint8_t sel_val)
{
    //uint32_t local_val = AIC_IOMUX->pad_gpiob_cfg_reg[gp_idx] & ~IOMUX_PAD_GPIOB_0_SEL_MASK;
    //AIC_IOMUX->pad_gpiob_cfg_reg[gp_idx] = local_val | ((sel_val << IOMUX_PAD_GPIOB_0_SEL_LSB) & IOMUX_PAD_GPIOB_0_SEL_MASK);

    int ret = 0;
    uint32_t local_val = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;

    read_addr = AIC_IOMUX_BASE + offsetof(AIC_IOMUX_TypeDef, pad_gpiob_cfg_reg[gp_idx]);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    local_val = mem_read_cfm.memdata & ~IOMUX_PAD_GPIOB_0_SEL_MASK;
    write_val = local_val | ((sel_val << IOMUX_PAD_GPIOB_0_SEL_LSB) & IOMUX_PAD_GPIOB_0_SEL_MASK);
    write_addr = AIC_IOMUX_BASE + offsetof(AIC_IOMUX_TypeDef, pad_gpiob_cfg_reg[gp_idx]);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_init(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    // iomux
    if ((gpidx < 2) || ((gpidx > 7) && (gpidx < 10))) {
        iomux_gpioa_config_sel_setf(rwnx_hw, gpidx, 1);
    } else if (gpidx < 16) {
        iomux_gpioa_config_sel_setf(rwnx_hw, gpidx, 0);
    }
    #if 0
    else if ((gpidx == 16) || (gpidx == 17)) {
        AIC_CPUSYSCTRL->cfg_usb_gpio |= CPU_SYS_CTRL_CFG_USB_GPIO_MODE;
        rwnx_send_dbg_mem_read_req(rwnx_hw, );
    }
    #endif

    // mask
    //AIC_GPIOA->MR |=  gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, MR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata | gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, MR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_deinit(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    // mask
    //AIC_GPIOA->MR &= ~gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, MR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata & ~gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, MR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_dir_in(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOA->DR &= ~gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata & ~gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_dir_out(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOA->DR |=  gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata | gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_set(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOA->VR |=  gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata | gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpioa_clr(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOA->VR &= ~gpmsk;
    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata & ~gpmsk;
    write_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

int gpioa_get(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int val;
    int ret = 0;
    uint32_t loval_val = 0;
    uint32_t read_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    read_addr = AIC_GPIOA_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    loval_val = mem_read_cfm.memdata;
    val = (loval_val & gpmsk) ? 1 : 0;
    return val;
}

void gpiob_init(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;

    if (gpidx < GPIOB_IDX_MAX) {
        unsigned int gpmsk = 0x01UL << gpidx;
        // iomux
        iomux_gpiob_config_sel_setf(rwnx_hw, gpidx, 0);

        // mask
        //AIC_GPIOB->MR |=  gpmsk;
        read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, MR);
        ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
        if (ret)
            printk("%s: %d, err %d\n", __func__, __LINE__, ret);
        AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

        write_val = mem_read_cfm.memdata | gpmsk;
        write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, MR);
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
        if (ret)
            printk("%s: %d, err %d\n", __func__, __LINE__, ret);
        AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
    }
}

void gpiob_deinit(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;

    if (gpidx < GPIOB_IDX_MAX) {
        unsigned int gpmsk = 0x01UL << gpidx;

        // mask
        //AIC_GPIOB->MR &= ~gpmsk;
        read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, MR);
        ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
        if (ret)
            printk("%s: %d, err %d\n", __func__, __LINE__, ret);
        AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

        write_val = mem_read_cfm.memdata & ~gpmsk;
        write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, MR);
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
        if (ret)
            printk("%s: %d, err %d\n", __func__, __LINE__, ret);
        AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
    }
}

void gpiob_dir_in(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOB->DR &= ~gpmsk;
    read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata & ~gpmsk;
    write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpiob_dir_out(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOB->DR |=  gpmsk;
    read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata | gpmsk;
    write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, DR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpiob_set(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOB->VR |=  gpmsk;
    read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata | gpmsk;
    write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

void gpiob_clr(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int ret = 0;
    uint32_t read_addr = 0;
    uint32_t write_val = 0;
    uint32_t write_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    //AIC_GPIOB->VR &= ~gpmsk;
    read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    write_val = mem_read_cfm.memdata & ~gpmsk;
    write_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, write_addr, write_val);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: write_addr=%x, write_val=%x\n", __func__, write_addr, write_val);
}

int gpiob_get(struct rwnx_hw *rwnx_hw, int gpidx)
{
    int val;
    int ret = 0;
    uint32_t loval_val = 0;
    uint32_t read_addr = 0;
    struct dbg_mem_read_cfm mem_read_cfm;
    unsigned int gpmsk = 0x01UL << gpidx;

    read_addr = AIC_GPIOB_BASE + offsetof(AIC_GPIO_TypeDef, VR);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, read_addr, &mem_read_cfm);
    if (ret)
        printk("%s: %d, err %d\n", __func__, __LINE__, ret);
    AICRFTESTDBG("%s: read_addr=%x, read_val=%x\n", __func__, read_addr, mem_read_cfm.memdata);

    loval_val = mem_read_cfm.memdata;
    val = (loval_val & gpmsk) ? 1 : 0;
    return val;
}

