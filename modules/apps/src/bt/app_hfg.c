#include "tgt_cfg_bt.h"

#if APP_SUPPORT_HFG
#include "rtos.h"
#include "aic_adp_api.h"
#include "app_bt_queue.h"

BT_ADDR test_device = {{0xaa,0xc0,0x00,0x88,0x88,0x33}};
void app_hfg_set_def_device(BT_ADDR bd_device)
{
    test_device = bd_device;
    TRACE("APP:app_hfg_set_def_device : %x,%x,%x,%x,%x,%x\n",test_device.addr[0],test_device.addr[1]\
        ,test_device.addr[2],test_device.addr[3],test_device.addr[4],test_device.addr[5]);
}

void app_hfg_connect_def_device(void)
{
    TRACE("APP:app_hfg_connect_def_device\n");
    app_bt_connect_hfg(&test_device);
}

void app_hfg_connect_sco(void)
{
    TRACE("APP:app_hfg_connect_sco\n");
    app_bt_hfp_connect_sco(&test_device);
}

void app_hfg_disconnect_sco(void)
{
    TRACE("APP:app_hfg_disconnect_sco\n");
    app_bt_hfp_disconnect_sco(&test_device);
}

void app_hfg_set_vgm(uint8_t vol)
{
    char Call_Mic_Gain_Command[]="\r\n+VGM: 15\r\n";

    if(vol>=0 && vol<10){
        Call_Mic_Gain_Command[8] = 0x30;
        Call_Mic_Gain_Command[9] = 0x30+vol;
    }else if(vol>=10 && vol < 16){
        Call_Mic_Gain_Command[8] = 0x31;
        Call_Mic_Gain_Command[9] = 0x30+(vol-10);
    }else{
        TRACE("%s input value %d error out of range(0~15)\n",__func__,vol);
    }
    TRACE("HFP: %s,%s\n",__func__,Call_Mic_Gain_Command);
    for(int i = 0; i < sizeof(Call_Mic_Gain_Command); i++){
        TRACE("%x ",Call_Mic_Gain_Command[i]);
    }
    TRACE("\n");
    aic_adp_hfp_send_raw_data(test_device,Call_Mic_Gain_Command);
}

void app_hfg_set_vgs(uint8_t vol)
{
    char Call_Spker_Gain_Command[]="\r\n+VGS: 15\r\n";
    if(vol>=0 && vol<10){
        Call_Spker_Gain_Command[8] = 0x30;
        Call_Spker_Gain_Command[9] = 0x30+vol;
    }else if(vol>=10 && vol < 16){
        Call_Spker_Gain_Command[8] = 0x31;
        Call_Spker_Gain_Command[9] = 0x30+(vol-10);
    }else{
        TRACE("%s input value %d error out of range(0~15)\n",__func__,vol);
    }
    TRACE("HFP: %s,%s\n",__func__,Call_Spker_Gain_Command);
    for(int i = 0; i < sizeof(Call_Spker_Gain_Command); i++){
        TRACE("%x ",Call_Spker_Gain_Command[i]);
    }
    TRACE("\n");
    aic_adp_hfp_send_raw_data(test_device,Call_Spker_Gain_Command);
}

#endif
