/**
 ****************************************************************************************
 *
 * @file arch_main.c
 *
 * @brief Main loop of the application.
 *
 ****************************************************************************************
 */

/*
 * INCLUDES
 ****************************************************************************************
 */

#include "plf.h"       // SW configuration

#include "arch.h"      // architectural platform definitions
#include <stdlib.h>    // standard lib functions
#include "boot.h"      // boot definition
#include "sysctrl_api.h"
#include "gpio_api.h"

#include "dbg.h"

#include "stdio_uart.h"
#include "system.h"

#if (PLF_CONSOLE)
#include "console.h"
#endif /* PLF_CONSOLE */

#if PLF_TEST
#include "test_main.h"
#endif /* PLF_TEST */

#ifdef CFG_RTOS
#include "rtos.h"
#endif
#if defined(CFG_VER_STR)
#include "sdk_version.h"
#endif
#ifdef CONFIG_WDT
#include "wdt_api.h"
#endif /* CONFIG_WDT */

#if (PLF_PMIC)
#include "psim_api.h"
#include "pmu_rtc0.h"
#endif
#if PLF_BT_STACK || PLF_BLE_STACK
#include "bt_aic8800m80x2_drvif.h"
#endif

#if (PLF_PMIC)
#include "pmic_api.h"
#endif

/*
 * DEFINES
 ****************************************************************************************
 */

/** BANNER_STR show sw version and bulid time */
#define BANNER_STR \
    "\r\nChipTest [" __DATE__ "]\r\n\r\n"

/*
 * STRUCTURE DEFINITIONS
 ****************************************************************************************
 */

typedef void (*boot_func_t)(void);

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */
syscfg_predefined_t const syscfg_predefined = {
    .pmic_vcore_drop_en = CONFIG_LP_PMIC_VCORE_DROP_ENABLE,
    .pmic_vrtc08_ldo_off = 0,
    .pmic_vio_slp_pd_en = CONFIG_LP_PMIC_VIO_SLP_PD_ENABLE,
    .pmic_lp_clk_sel = CONFIG_LPCLK_SELECT,
    .sys_initial_clk_cfg = CONFIG_INITIAL_SYSTEM_CLOCK,
    .sys_vio_sel = CONFIG_VIO_SELECT,
    .sys_vflash_sel = CONFIG_VFLASH_SELECT,
    .sys_vrf_sel = CONFIG_VRF_SELECT,
    .xtal_cap_bit = CONFIG_XTAL_CAP_VALUE,
    .xtal_cap_fine_bit = CONFIG_XTAL_CAP_FINE_VALUE,
};

/*
 * LOCAL FUNCTION DECLARATIONS
 ****************************************************************************************
 */
#if (PLF_PMIC)
__STATIC_INLINE uint8_t pmic_rstcause_get(void)
{
    uint32_t reg_val = psim_read((unsigned int)(&AIC_PMU_RTC0->pmu_rtc0_cfg7));
    return (uint8_t)((reg_val & PMU_RTC0_RTC_RG_PWR_ON_SRC_STATUS_MASK) >> PMU_RTC0_RTC_RG_PWR_ON_SRC_STATUS_LSB);
}
#endif

/*
 * MAIN FUNCTION
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief main function.
 *
 * This function is called right after the booting process has completed.
 *
 * @return status   exit status
 ****************************************************************************************
 */
void rw_main(void)
{
    // Update system clocks
    SystemCoreClockUpdate();
    #if (PLF_PMIC)
    uint32_t boot_addr = PMIC_MEM_READ((unsigned int)&AIC_PMU_RTC0->rtc_rg_reserved0);
    PMIC_MEM_MASK_WRITE((unsigned int)&AIC_PMU_RTC0->rtc_rg_reserved0, 0, 0xffffffff);
    #endif
    #if PLF_BT_TESTMODE
    if (boot_addr != 0 && ((boot_addr&0x80000000)))
    {
        bt_drv_non_signaling_test_iram_init();
        boot_go(boot_addr & 0x7fffffff);
    }
    #endif
    #if PLF_BT_STACK || PLF_BLE_STACK
    extern struct aicbt_info_t aicbt_info;
    if (boot_addr != 0 && ((boot_addr&0x80000000) && (boot_addr & 0x7fffffff == 0)))
    {
        aicbt_info.btport = 2;
        aicbt_info.lpm_enable = 0;
        aicbt_info.uart_flowctrl = 0;
    }
    #endif
    // Initialize console
    #if (PLF_CONSOLE)
    console_init();
    #else
    // Initialize stdio uart
    stdio_uart_init();
    #endif

    #if 0//def CONFIG_WDT
    if (wdt_func_enabled()) {
        wdt_init(120);
    }
    #endif /* CONFIG_WDT */

    // finally start interrupt handling
    GLOBAL_INT_START();

    dbg(D_DBG D_CRT BANNER_STR);
    #if defined(CFG_VER_STR)
    dbg("\nVer: %s\nsdk build: %s\nusr build: %s\n", sdk_version_str, sdk_build_date, usr_build_date);
    #else
    dbg("\r\nchip_test start\r\n");
    #endif
    #if PLF_BT_STACK || PLF_BLE_STACK
    dbg("boot_addr:0x%x,btport:%d,lpm_enable:%d,uart_flowctrl:%d\n",boot_addr,aicbt_info.btport,aicbt_info.lpm_enable,aicbt_info.uart_flowctrl);
    #endif

    #ifndef CFG_RTOS
    // Initialize console
    #if (PLF_CONSOLE)
    console_init();
    #endif
    #if PLF_TEST
    test_main();
    #endif /* PLF_TEST */

    /*
     ************************************************************************************
     * Main loop
     ************************************************************************************
     */
    while (1) {

        // schedule all pending console commands
        #if (PLF_CONSOLE)
        console_schedule();
        #endif

        #if 0//defined(CONFIG_WDT)
        if (wdt_func_enabled()) {
            wdt_kick();
        }
        #endif /* CONFIG_WDT */

        GLOBAL_INT_DISABLE();
        #if (PLF_CONSOLE)
        if (console_buf_empty() == 1)
        #endif
        {
            // Wait for interrupt
            __WFI();
        }
        GLOBAL_INT_RESTORE();
    }
    #else
    rtos_main();
    #endif
}
