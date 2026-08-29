/*
 * Copyright (C) 2018-2025 AICSemi Ltd.
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
#include "sleep_api.h"
#include "gpio_api.h"
#include "wdt_api.h"
#include "stdio_uart.h"
#include "uart1_api.h"
#include "sysctrl_api.h"
#if (PLF_PMIC)
#include "pmic_api.h"
#endif
#include "dbg.h"

/**
 * Macros
 */
#define DEBUG_LP_REGS_EN    0

/**
 * TypeDefs
 */

/**
 * Variables
 */

/**
 * Functions
 */

/* Save the CPU_SYS peripherals */
__RAMTEXT void back_up_cpusys_pers(void)
{
    // back up resources host used
    gpio_regs_save();
    wdt_regs_save();
}

/* Restore the CPU_SYS peripherals */
__RAMTEXT void restore_cpusys_pers(void)
{
    // restore resources host used
    stdio_uart_recover();
    gpio_regs_recover();
    wdt_regs_recover();
}

void sleep_user_recover_pers(void)
{
    // e.g. uart1_recover
    //uart1_recover();
}

void sleep_user_callback_before_sleep(void)
{
    #if DEBUG_LP_REGS_EN
    uint32_t pwrctrl_sw_irqst = pwrctrl_sw_rec_irqstatus_get();
    dbg("pwrctrl_irqst=%x\n", pwrctrl_sw_irqst);
    dump_mem((uint32_t)&AIC_PWRCTRL->cpu_sys_pwr_ctrl, 27, 4, false);
    #endif
}
