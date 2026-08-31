#ifndef _APP_HFG_H_
#define _APP_HFG_H_
#if APP_SUPPORT_HFG == 1
void app_hfg_set_def_device(BT_ADDR bd_device);
void app_hfg_connect_def_device(void);
void app_hfg_connect_sco(void);
void app_hfg_disconnect_sco(void);
void app_hfg_set_vgm(uint8_t vol);
void app_hfg_set_vgs(uint8_t vol);
#endif
#endif
