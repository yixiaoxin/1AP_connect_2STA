/**
 ****************************************************************************************
 *
 * @file rtos.c
 *
 * @brief Entry point for WiFi stack integration within an RTOS.
 *
 ****************************************************************************************
 */
#include "rtos.h"
#include "dbg.h"
#include "plf.h"
#include "hw_cfg_api.h"

#if PLF_CONSOLE
#include "console_task.h"
#endif /* PLF_CONSOLE */

#if PLF_LETTER_SHELL
#include "shell_port.h"
#endif /* PLF_LETTER_SHELL */

#if PLF_MODULE_SOFTWDG
#include "softwdg.h"
#endif

#if PLF_ASIO
#include "asio.h"
#endif /* PLF_ASIO */

#if PLF_AUDIO
#include "app_audio.h"
#include "audio_eq.h"
#include "audio_drc.h"
#endif /* PLF_AUDIO */

#if PLF_TEST
#include "test_main.h"
#endif /* PLF_TEST */

#if PLF_BT_STACK
#if PLF_BLE_ONLY == 0
#include "bt_task.h"
#else
#include "ble_task.h"
#endif
#if PLF_AON_SUPPORT
#include "bt_aon_sram.h"
#endif
#endif /* PLF_BT_STACK */

#if PLF_WIFI_STACK
#ifdef CFG_HOSTIF
#include "hostif.h"
#endif
#ifdef CONFIG_RWNX_LWIP
#include "net_al.h"
#include "fhost.h"
#include "fhost_wpa.h"
#ifdef CFG_HOSTIF
#include "rwnx_defs.h"
struct rwnx_hw hw_env = {NULL,};
#endif /* CFG_HOSTIF */
#endif /* CONFIG_RWNX_LWIP */
#if !defined(CFG_HOSTIF) || defined(CFG_DEVICE_IPC)
#include "rwnx_defs.h"
struct rwnx_hw hw_env = {NULL,};
#endif
#endif /* PLF_WIFI_STACK */

#include "co_main.h"
#include "sysctrl_api.h"
#include "sleep_api.h"

#if PLF_DSP
#include "dsp_task.h"
#endif /* PLF_DSP */

#if PLF_MODULE_TEMP_COMP
#include "temp_comp.h"
#endif

#if PLF_PMIC && (!PLF_AIC8800)
#include "pmic_api.h"
#endif

#ifdef CFG_USER_CODE
#include "demo_src.h"
#endif

#if PLF_BT_STACK
void aic_bt_start(void);
#endif


/*
 * STA 关机态 HIBERNATE fast boot：
 *
 * HIBERNATE 唤醒属于冷启动。如果关机态短按/长按按键后，仍然先跑
 * temp_comp / Wi-Fi / net / fhost / BT 等初始化，蓝灯响应会被拖慢。
 *
 * fast boot 只用于 soft-off flag 已存在的启动：
 * - 短按：user_code_entry() 内亮蓝灯后重新回 HIBERNATE；
 * - 长按：清 soft-off flag 后软件重启，再走完整初始化。
 *
 * 注意：该宏只应给 STA/圆饼麦固件打开，AP 固件不要打开。
 */
#ifndef APP_ENABLE_HIBERNATE_SOFTOFF_FASTBOOT
#define APP_ENABLE_HIBERNATE_SOFTOFF_FASTBOOT 0
#endif

static uint8_t s_rtos_softoff_fast_boot = 0;

uint8_t rtos_softoff_fast_boot_is_set(void)
{
    return s_rtos_softoff_fast_boot;
}

#if APP_ENABLE_HIBERNATE_SOFTOFF_FASTBOOT && defined(CFG_USER_CODE)
extern uint8_t app_low_power_softoff_flag_is_set(void) __attribute__((weak));

static uint8_t rtos_softoff_fast_boot_check(void)
{
    if (app_low_power_softoff_flag_is_set &&
        app_low_power_softoff_flag_is_set()) {
        return 1;
    }

    return 0;
}
#else
static uint8_t rtos_softoff_fast_boot_check(void)
{
    return 0;
}
#endif


rtos_task_cfg_st get_task_cfg(uint8_t task_id)
{
    rtos_task_cfg_st cfg = {0, 0};

    switch (task_id) {
        case IPC_CNTRL_TASK:
            cfg.priority   = TASK_PRIORITY_WIFI_IPC;
            cfg.stack_size = TASK_STACK_SIZE_WIFI_IPC;
            break;
        case SUPPLICANT_TASK:
            cfg.priority   = TASK_PRIORITY_WIFI_WPA;
            cfg.stack_size = TASK_STACK_SIZE_WIFI_WPA;
            break;
        case CONTROL_TASK:
            cfg.priority   = TASK_PRIORITY_WIFI_CNTRL;
            cfg.stack_size = TASK_STACK_SIZE_WIFI_CNTRL;
            break;
        case APP_FHOST_TX_TASK:
            cfg.priority   = TASK_PRIORITY_WIFI_TX;
            cfg.stack_size = TASK_STACK_SIZE_WIFI_TX;
            break;
        case HOSTAPD_TASK:
            cfg.priority   = TASK_PRIORITY_WIFI_HOSTAPD;
            cfg.stack_size = TASK_STACK_SIZE_WIFI_HOSTAPD;
            break;
        default:
            break;
    }

    return cfg;
}

/**
 * Save user data that declared with PRIVATE_HOST_*(G3USER)
 */
__WEAK void user_data_save(void)
{
    // VOID
}

/**
 * Restore user data that declared with PRIVATE_HOST_*(G3USER)
 */
__WEAK void user_data_restore(void)
{
    // VOID
}

#ifndef CFG_WAPI
__WEAK void wapi_main(void)
{
    // VOID
}
__WEAK void wapi_eloop_register_read_sock(void)
{
    // VOID
}
int wapi_driver_rwnx_ops = 0;
#endif
void rtos_main(void)
{
    dbg("Enter rtos_main\r\n");

    #if (PLF_HW_PXP == 1)
    dbg("RUNNING IN SIMULATION MODE\r\n");
    #endif

    #if DBG_MUTEX_ENABLED
    dbg_rtos_init();
    #endif

    if (rtos_init())
    {
        ASSERT_ERR(0);
    }

    s_rtos_softoff_fast_boot = rtos_softoff_fast_boot_check();
    if (s_rtos_softoff_fast_boot) {
        dbg("RTOS: soft-off fast boot, skip temp_comp/wifi/net/fhost/bt/audio\r\n");
    }

    #if PLF_MODULE_TEMP_COMP
    if (!s_rtos_softoff_fast_boot) {
        temp_comp_init();
        temp_comp_start();
    } else {
        dbg("RTOS: skip temp_comp\r\n");
    }
    #endif

    #if PLF_BT_STACK
    if (!s_rtos_softoff_fast_boot) {
        aic_bt_start();
    } else {
        dbg("RTOS: skip aic_bt_start\r\n");
    }
    #endif

    #if PLF_WIFI_STACK
    if (!s_rtos_softoff_fast_boot) {
    #if defined(CONFIG_RWNX_LWIP)
        wifi_patch_prepare();
        rwnx_ipc_init(&hw_env, &ipc_shared_env);
    #endif /* CONFIG_RWNX_LWIP */
    #if defined(CFG_DEVICE_IPC)
        rwnx_ipc_init(&hw_env, &ipc_shared_env);
    #endif
    } else {
        dbg("RTOS: skip wifi ipc init\r\n");
    }
    #endif /* PLF_WIFI_STACK */

    #if PLF_PMIC && (!PLF_AIC8800)
    pmic_boot_check();
    #endif /* PLF_PMIC && (!PLF_AIC8800) */

    #if (PLF_CONSOLE && !(PLF_WIFI_STACK && (defined(CFG_APP_CONSOLEWIFI) || defined(CFG_APP_UARTWIFI))))
    console_task_init();
    #endif /* PLF_CONSOLE && !PLF_WIFI_STACK */

    #if PLF_LETTER_SHELL
    userShellInit();
    #endif

    #if PLF_MODULE_SOFTWDG
    softwdg_task_init();
    #endif

    #if PLF_AUD_USED
    if (!s_rtos_softoff_fast_boot) {
    #if PLF_ASIO
        asio_init();
    #endif

    #if PLF_AUDIO
        app_audio_open((uint32_t)&__HeapLimit[0], (uint32_t)&__StackLimit[0]);
        audio_eq_init();
        audio_drc_init();
    #if PLF_CONSOLE
        audio_eq_cmd_init();
        audio_drc_cmd_init();
    #endif
    #endif
    } else {
        dbg("RTOS: skip audio/asio init\r\n");
    }
    #endif

    #if PLF_TEST
    test_task_init();
    #endif /* PLF_TEST */

    #if PLF_BT_STACK
    if (!s_rtos_softoff_fast_boot) {
    #if !(defined(CFG_TEST_AF) || defined(CFG_TEST_SBC) || defined(CFG_TEST_AAC) || defined(CFG_TEST_SDCARD_AUDIO) || defined(CFG_TEST_HCI))
    #if PLF_AON_SUPPORT
        host_aon_interface_init();
    #endif
    #if PLF_BLE_ONLY == 0
        bt_task_init();
    #else
        ble_task_init(INIT_NORMAL);
    #endif
    #if PLF_AON_SUPPORT
        cpup_ready_set(true);
    #endif
    #endif
    } else {
        dbg("RTOS: skip bt/ble task init\r\n");
    }
    #endif /* PLF_BT_STACK */

    #ifdef CFG_HOSTIF
    if (!s_rtos_softoff_fast_boot) {
        init_host(0);
    } else {
        dbg("RTOS: skip init_host\r\n");
    }
    #endif
    #if PLF_WIFI_STACK
    #ifdef CONFIG_RWNX_LWIP
    if (!s_rtos_softoff_fast_boot) {
        net_init();
        // Initialize the FHOST module
        fhost_init(NULL);
    } else {
        dbg("RTOS: skip net/fhost\r\n");
    }
    #endif /* CONFIG_RWNX_LWIP */
    #endif /* PLF_WIFI_STACK */

    co_main_init();
    hw_cfg_init();

    #if (PLF_AIC8800)
    if (pwrctrl_pwrmd_cpusys_sw_record_getf() >= CPU_SYS_POWER_DOWN) {
        // restore data
        sleep_data_restore();
        #if PLF_WIFI_STACK
        #ifdef CONFIG_RWNX_LWIP
        if (!s_rtos_softoff_fast_boot) {
            aon_mailbox_restore();
            fhost_data_restore();
            lwip_data_restore();
            wpas_data_restore();
        } else {
            dbg("RTOS: skip wifi/lwip/wpas restore\r\n");
        }
        #endif /* CONFIG_RWNX_LWIP */
        #endif /* PLF_WIFI_STACK */
        user_data_restore();
        sys_wakeup_indicate();
        #if PLF_BT_STACK
        #if PLF_BLE_ONLY
        if (!s_rtos_softoff_fast_boot) {
            ble_task_restore();
        } else {
            dbg("RTOS: skip ble restore\r\n");
        }
        #endif
        #endif
    }
    #endif

    #if PLF_DSP
    dsp_task_init();
    #endif

    #ifdef CFG_USER_CODE
    user_code_entry();
    #endif

    // Start the scheduler
    rtos_start_scheduler();

    // Should never reach here
    for( ;; );
}
