/*
 * Copyright (C) 2018-2024 AICSemi Ltd.
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
#include "wlan_user.h"
#include "dbg.h"
#include "boot.h"
#include "fhost.h"
#include "fhost_config.h"
#include "rwnx_msg_tx.h"
#include <stdint.h>

#if defined(CFG_USER_CODE)
extern uint8_t app_low_power_softoff_flag_is_set(void) __attribute__((weak));
#endif

#if (PLF_AIC8800M40 && PLF_MODULE_TEMP_COMP)
#include "temp_comp.h"
#include "flash_api_wifi.h"
#endif

/**
 * Macros
 */

/**
 * TypeDefs
 */

/**
 * Variables
 */

/**
 * Functions
 */

void wlan_process_ipc_corruption(void)
{
    dbg("wifi ipc corrupted!\n");

    /* USER CODE BEGIN */
    //panic();
    /* USER CODE END */
}

#if (PLF_AIC8800M40) || (PLF_AIC8800MC)
/**
 * Refer to "debugfs使用文档v1.2.pdf"
 */
void wlan_ipcam_setting(void)
{
    struct mm_set_channel_access_req edca_req;
    struct mm_set_cca_threshold_req  cca_req;

    int32_t param[14];
    int32_t cca[5]= {0x10, 0, 0, 0, 0};

    param[0] = 0; param[1] = 0x5e222; param[2] = 0; param[3] = 0;
    param[4] = 0;// TODO: rwnx_vif->vif_index;
    param[5] = 0x1e; param[6] = 0; param[7] = 0; param[8] =0;
    param[9] = 0x5;param[10] = 0x5;param[11] = 0x5;param[12] = 0;param[13] = 0;
    edca_req.hwconfig_id     = CHANNEL_ACCESS_REQ;
    edca_req.edca[0]         = param[0];
    edca_req.edca[1]         = param[1];
    edca_req.edca[2]         = param[2];
    edca_req.edca[3]         = param[3];
    edca_req.vif_idx         = param[4];
    edca_req.retry_cnt       = param[5];
    edca_req.rts_en          = param[6];
    edca_req.long_nav_en     = param[7];
    edca_req.cfe_en          = param[8];
    edca_req.rc_retry_cnt[0] = param[9];
    edca_req.rc_retry_cnt[1] = param[10];
    edca_req.rc_retry_cnt[2] = param[11];
    edca_req.ccademod_th     = param[12];
    edca_req.remove_1m2m     = param[13];
    rwnx_send_vendor_hwconfig_req(&edca_req);

    cca_req.hwconfig_id = CCA_THRESHOLD_REQ;
    cca_req.auto_cca_en    = cca[0];
    cca_req.cca20p_rise_th = cca[1];
    cca_req.cca20s_rise_th = cca[2];
    cca_req.cca20p_fall_th = cca[3];
    cca_req.cca20s_fall_th = cca[4];
    rwnx_send_vendor_hwconfig_req(&cca_req);
}
#endif

void wlan_initialize_user_setting(void)
{
    #if PLF_AIC8800M40 && PLF_MODULE_TEMP_COMP
    {
        int ret;
        struct mm_set_vendor_swconfig_req req = {0,};
        struct mm_set_vendor_swconfig_cfm cfm = {0,};

        /*
         * HIBERNATE soft-off 冷启动时，业务还没有真正开机。
         * 这时如果重新打开 TEMP_COMP_SET_REQ 15s 周期源，关机态会被周期唤醒，
         * 然后 app_i2s_pcm_lower_wifi.c 的短按提示分支会误亮蓝灯。
         */
#if defined(CFG_USER_CODE)
        if (app_low_power_softoff_flag_is_set && app_low_power_softoff_flag_is_set()) {
            dbg("LP_PERIODIC: soft-off boot, skip TEMP_COMP_SET_REQ enable\n");
        } else
#endif
        {
            // send vendor swconfig req
            req.swconfig_id = TEMP_COMP_SET_REQ;
            req.temp_comp_set_req.enable = 1;
            req.temp_comp_set_req.reserved[0] = (uint8_t)temp_comp_calibed_temp_level_get();
            req.temp_comp_set_req.tmr_period_ms  = temp_comp_timer_period_ms_get();
            ret = rwnx_send_vendor_swconfig_req(&req, &cfm);
            if (!ret) {
                dbg("temp_comp status=0x%x\n", cfm.temp_comp_set_cfm.status);
            } else {
                dbg("send TEMP_COMP_SET_REQ fail, ret=%d\n", ret);
                return;
            }
        }

        #if TEMP_COMP_DPD_RES_IN_FLASH
        // enbale dynamic dpd based on config
        if (fhost_usr_cfg.wifi_ext_flags & WIFI_EXT_DYNAMIC_DPD_CALIB_EN_FLAG) {
            // send vendor swconfig req
            req.swconfig_id = EXT_FLAGS_MASK_SET_REQ;
            req.ext_flags_mask_set_req.user_flags_mask = USER_DYNAMIC_DPD_CALIB_FLAG;
            req.ext_flags_mask_set_req.user_flags_val  = USER_DYNAMIC_DPD_CALIB_FLAG;
            ret = rwnx_send_vendor_swconfig_req(&req, &cfm);
            if (!ret) {
                dbg("user_flags=0x%x\n", cfm.ext_flags_mask_set_cfm.user_flags);
            } else {
                dbg("send EXT_FLAGS_MASK_SET_REQ fail, ret=%d\n", ret);
                return;
            }
        }
        #endif
    }
    #endif

    #if (PLF_AIC8800M40) || (PLF_AIC8800MC)
    #if (CONFIG_WIFI_IPCAM)
    wlan_ipcam_setting();
    #endif
    #endif

    /* USER CODE BEGIN */
    /* USER CODE END */
}

#if (PLF_AIC8800M40)
void wlan_dynamic_dpd_cal_res_indication(int ch_grp_idx, int dpd_temp_lvl, int dpd_degree_val, unsigned int rf_misc_ram_addr)
{
    #if PLF_MODULE_TEMP_COMP && TEMP_COMP_DPD_RES_IN_FLASH
    if (fhost_usr_cfg.wifi_ext_flags & WIFI_EXT_DYNAMIC_DPD_CALIB_EN_FLAG) {
        wifi_rf_cal_info_t *buf_tmp;
        temp_comp_dpd_cal_curr_ch_idx_set(ch_grp_idx);
        buf_tmp = rtos_malloc(sizeof(wifi_rf_cal_info_t));
        if (buf_tmp) {
            int ret;
            dbg("store dpd_res ch=%d lvl=%d t=%d into flash\n", ch_grp_idx, dpd_temp_lvl, dpd_degree_val);
            ret = flash_wifi_dpd_cal_res_write(ch_grp_idx, dpd_temp_lvl, dpd_degree_val, buf_tmp, (void *)rf_misc_ram_addr);
            rtos_free(buf_tmp);
            if (ret) {
                dbg("wifi_rf_cal_info fls wr fail %d, %d, %x\n", ch_grp_idx, dpd_temp_lvl, rf_misc_ram_addr);
            }
        } else {
            dbg("wifi_rf_cal_info ram alloc fail\n");
        }
    }
    #endif
}
#endif
