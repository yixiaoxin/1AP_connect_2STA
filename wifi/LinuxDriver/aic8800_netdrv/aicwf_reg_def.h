/**
 * aicwf_reg_api.h
 *
 * rftest cmd declarations
 *
 * Copyright (C) AICSemi 2018-2025
 */

#ifndef _AICWF_REG_DEF_H_
#define _AICWF_REG_DEF_H_

#include <linux/types.h>

// gpio_api.h
#define GPIOA_IDX_MAX   (16)
#define GPIOB_IDX_MAX   (8)

// reg_iomux.h
#define IOMUX_PAD_GPIOA_0_SEL_LSB       (0)
#define IOMUX_PAD_GPIOA_0_SEL_MASK      (15<<0)
#define IOMUX_PAD_GPIOA_0_SEL_MASK      (15<<0)
#define IOMUX_PAD_GPIOB_0_SEL_LSB       (0)
#define IOMUX_PAD_GPIOB_0_SEL_MASK      (15<<0)
#define IOMUX_PAD_GPIOB_0_SEL_MASK      (15<<0)

// chip.h
#define AIC_APB0_BASE         (0x40100000UL)
#define AIC_AHB1_BASE         (0x40500000UL)
#define AIC_APB1_BASE         (AIC_AHB1_BASE)
#define AIC_CPUSYSCTRL_BASE   (AIC_APB0_BASE + 0x00000)
#define AIC_IOMUX_BASE        (AIC_APB1_BASE + 0x04000)
#define AIC_GPIOA_BASE        (AIC_APB0_BASE + 0x0B000)
#define AIC_GPIOB_BASE        (AIC_APB1_BASE + 0x05000)

// reg_sysctrl.h
#define CPU_SYS_CTRL_CFG_USB_GPIO_MODE      (1<<0)

/* ================================================================================ */
/* ================                      IOMUX                     ================ */
/* ================================================================================ */
typedef struct {
  union {
    u32 pad_gpioa_cfg_reg[16]; //0x00000000
    struct {
      u32                     pad_gpioa_0_cfg_reg;          //0x00000000
      u32                     pad_gpioa_1_cfg_reg;          //0x00000004
      u32                     pad_gpioa_2_cfg_reg;          //0x00000008
      u32                     pad_gpioa_3_cfg_reg;          //0x0000000C
      u32                     pad_gpioa_4_cfg_reg;          //0x00000010
      u32                     pad_gpioa_5_cfg_reg;          //0x00000014
      u32                     pad_gpioa_6_cfg_reg;          //0x00000018
      u32                     pad_gpioa_7_cfg_reg;          //0x0000001C
      u32                     pad_gpioa_8_cfg_reg;          //0x00000020
      u32                     pad_gpioa_9_cfg_reg;          //0x00000024
      u32                     pad_gpioa_10_cfg_reg;         //0x00000028
      u32                     pad_gpioa_11_cfg_reg;         //0x0000002C
      u32                     pad_gpioa_12_cfg_reg;         //0x00000030
      u32                     pad_gpioa_13_cfg_reg;         //0x00000034
      u32                     pad_gpioa_14_cfg_reg;         //0x00000038
      u32                     pad_gpioa_15_cfg_reg;         //0x0000003C
    };
  };
  union {
    u32 pad_agpio_cfg_reg[16]; //0x00000040
    struct {
      u32                     pad_agpio_0_cfg_reg;          //0x00000040
      u32                     pad_agpio_1_cfg_reg;          //0x00000044
      u32                     pad_agpio_2_cfg_reg;          //0x00000048
      u32                     pad_agpio_3_cfg_reg;          //0x0000004C
      u32                     pad_agpio_4_cfg_reg;          //0x00000050
      u32                     pad_agpio_5_cfg_reg;          //0x00000054
      u32                     pad_agpio_6_cfg_reg;          //0x00000058
      u32                     pad_agpio_7_cfg_reg;          //0x0000005C
      u32                     pad_agpio_8_cfg_reg;          //0x00000060
      u32                     pad_agpio_9_cfg_reg;          //0x00000064
      u32                     pad_agpio_10_cfg_reg;         //0x00000068
      u32                     pad_agpio_11_cfg_reg;         //0x0000006C
      u32                     pad_agpio_12_cfg_reg;         //0x00000070
      u32                     pad_agpio_13_cfg_reg;         //0x00000074
      u32                     pad_agpio_14_cfg_reg;         //0x00000078
      u32                     pad_agpio_15_cfg_reg;         //0x0000007C
    };
  };
  union {
    u32 pad_gpiob_cfg_reg[8]; //0x00000080
    struct {
      u32                     pad_gpiob_0_cfg_reg;          //0x00000080
      u32                     pad_gpiob_1_cfg_reg;          //0x00000084
      u32                     pad_gpiob_2_cfg_reg;          //0x00000088
      u32                     pad_gpiob_3_cfg_reg;          //0x0000008C
      u32                     pad_gpiob_4_cfg_reg;          //0x00000090
      u32                     pad_gpiob_5_cfg_reg;          //0x00000094
      u32                     pad_gpiob_6_cfg_reg;          //0x00000098
      u32                     pad_gpiob_7_cfg_reg;          //0x0000009C
    };
  };
} AIC_IOMUX_TypeDef;

// reg_sysctrl.h
/* ================================================================================ */
/* ================                CPU System Control              ================ */
/* ================================================================================ */
typedef struct {
  u32                          hclk_ctrl_mode;               //0x00000000
  u32                          hclk1_ctrl_mode;              //0x00000004
  u32                          pclk_ctrl_mode;               //0x00000008
  u32                          others_clk_ctrl_mode;         //0x0000000C
  u32                          cfg_mem_rom;                  //0x00000010
  u32                          cfg_mem_1prf;                 //0x00000014
  u32                          cfg_mem_2prf;                 //0x00000018
  u32                          trap_bypass;                  //0x0000001C
  u32                          cache_bypass;                 //0x00000020
  u32                          pclk_div;                     //0x00000024
  u32                          codec_mclk_div;               //0x00000028
  u32                          bclk_sel;                     //0x0000002C
  u32                          cpu_sys_tports_sel;           //0x00000030
  u32                          phy_dump_sel;                 //0x00000034
  u32                          clk_msadc_div;                //0x00000038
  u32                          cfg_cpu_stclk_div;            //0x0000003C
  u32                          cfg_usb_gpio;                 //0x00000040
  u32                          cfg_cpu_p_rxev;               //0x00000044
  u32                          cfg_cpu_s0_rxev;              //0x00000048
  u32                          cfg_cpu_nmi_en;               //0x0000004C
  u32                          ro_top_tports;                //0x00000050
  u32 Reserved_00000054[43];                //0x00000054
  u32                          hclk_manual_enable;           //0x00000100
  u32                          hclk_manual_disable;          //0x00000104
  u32                          hclk1_manual_enable;          //0x00000108
  u32                          hclk1_manual_disable;         //0x0000010C
  u32                          pclk_manual_enable;           //0x00000110
  u32                          pclk_manual_disable;          //0x00000114
  u32                          others_manual_enable;         //0x00000118
  u32                          others_manual_disable;        //0x0000011C
  u32                          hclk_soft_resetn_set;         //0x00000120
  u32                          hclk_soft_resetn_clr;         //0x00000124
  u32                          hclk1_soft_resetn_set;        //0x00000128
  u32                          hclk1_soft_resetn_clr;        //0x0000012C
  u32                          pclk_soft_resetn_set;         //0x00000130
  u32                          pclk_soft_resetn_clr;         //0x00000134
  u32                          others_soft_resetn_set;       //0x00000138
  u32                          others_soft_resetn_clr;       //0x0000013C
} AIC_CPUSYSCTRL_TypeDef;

// reg_gpio.h
/* ========================================================================== */
/* ================    General Purpose Input/Output (GPIO)   ================ */
/* ========================================================================== */
typedef struct {
    u32 VR;           /* 0x000 (R/W) : Val Reg */
    u32 MR;           /* 0x004 (R/W) : Msk Reg */
    u32 DR;           /* 0x008 (R/W) : Dir Reg */
    u32 TELR;         /* 0x00C (R/W) : Trig Edg or Lvl Reg */
    u32 TER;          /* 0x010 (R/W) : Trig Edg Reg */
    u32 TLR;          /* 0x014 (R/W) : Trig Lvl Reg */
    u32 ICR;          /* 0x018 (R/W) : Int Ctrl Reg */
    u32 RISR;         /* 0x01C (R)   : raw interrupt status register */
    u32 ISR;          /* 0x020 (R)   : Mask Int Stat Reg */
    u32 IRR;          /* 0x024 (W)   : Int Rm Reg */
    u32 TIR;          /* 0x028 (R/W) : Trig In Reg */
    u32 FR;           /* 0x02C (R/W) : Fltr Reg */
} AIC_GPIO_TypeDef;


#endif /* _AICWF_REG_DEF_H_ */

