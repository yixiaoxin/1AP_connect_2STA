#include "plf.h"
#include "system.h"
#include "bt_hci.h"
#include "bt_aic8800mc_drv_config.h"
#include "dbg.h"
#include "dbg_assert.h"

#define AICBT_FW_DEFAULT_START_ADDR              0x50000
#define AICBT_FW_RAM_START_ADDR                  0x100000

static uint32_t bt_fw_start_addr = AICBT_FW_DEFAULT_START_ADDR;
static uint32_t bt_hci_ram_base[BT_HCI_CH_NUM] = {0,};

/*
 * set bt fw addr
 * @param addr : if value is 0 or this function is not used, it will use default fw addr;
 */
void bt_common_set_defalut_fw_addr(uint32_t addr)
{
    if(addr && addr != bt_fw_start_addr){
        bt_fw_start_addr = addr;
        TRACE("fw_start_addr = 0x%x\n",bt_fw_start_addr);
    }
}

uint32_t bt_common_get_defalut_fw_addr(void)
{
    return bt_fw_start_addr;
}

/*
 * set bt hci ipc ram base
 * @param ram_base[BT_HCI_CH_NUM] : if value is 0 or this function is not used, it will use default ipc ram base;
 */
void bt_common_set_default_ram_base(uint32_t *ram_base)
{
    for(uint8_t i = 0; i < BT_HCI_CH_NUM; i++){
        if (ram_base[i]) {
            bt_hci_ram_base[i] = ram_base[i];
            TRACE("hci_ram_base %x = 0x%x\n",i,bt_hci_ram_base[i]);
        }
    }
}

uint32_t *bt_common_get_default_ram_base(void)
{
    return &bt_hci_ram_base[0];
}

/*
 * bt_common_change_fw_load_in_ram
 * only used for debug fw in ram addr ,fw in rom will mask this function.
 */
void bt_common_change_fw_load_in_ram(void)
{
    uint32_t mb_base[BT_HCI_CH_NUM] = {(0x00170000 + 0x10000 - 0x100)};
    bt_common_set_default_ram_base(&mb_base[0]);
    bt_common_set_defalut_fw_addr(AICBT_FW_RAM_START_ADDR);
}

const void* aic_fw_ptr_get(enum aic_fw name)
{
    void *ptr = NULL;
    uint32_t chipid;
    uint8_t rom_ver;
#if (CFG_ROM_VER == 255)
    rom_ver = ChipRomVerGet();
#else
    rom_ver = CFG_ROM_VER;
#endif
    chipid = ChipIdGet(1);
    dbg("rom ver %d,chipid %d\n",rom_ver,chipid);

    switch(name){
        case FW_ADID_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        ptr = &fw_adid_u02[0];
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        ptr = &fw_adid_u02h[0];
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        ptr = &fw_patch_u02[0];
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        ptr = &fw_patch_u02h[0];
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_8800DC_EXT:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        ptr = &fw_patch_u02_ext0[0];
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        ptr = &fw_patch_u02h_ext0[0];
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_TABLE_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        ptr = &fw_patch_table_u02[0];
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        ptr = &fw_patch_table_u02h[0];
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        default:
            dbg("%s ptr is NULL\n",__func__);
            break;
    }
    return ptr;
}

int aic_fw_size_get(enum aic_fw name)
{
    int size = 0;
    uint32_t chipid;
    uint8_t rom_ver;
#if (CFG_ROM_VER == 255)
    rom_ver = ChipRomVerGet();
#else
    rom_ver = CFG_ROM_VER;
#endif
    chipid = ChipIdGet(1);
    dbg("rom ver %d,chipid %d\n",rom_ver,chipid);
    switch(name){
        case FW_ADID_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        size = AICBT_FW_ADID_U02_SIZE;
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        size = AICBT_FW_ADID_U02H_SIZE;
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        size = AICBT_FW_PATCH_U02_SIZE;
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        size = AICBT_FW_PATCH_U02H_SIZE;
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_8800DC_EXT:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        size = AICBT_FW_PATCH_U02_EXT0_SIZE;
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        size = AICBT_FW_PATCH_U02H_EXT0_SIZE;
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
        case FW_PATCH_TABLE_8800DC:
            {
                if(chipid == 0){
                    switch (rom_ver) {
                    case 1:
                        size = AICBT_FW_PATCH_TABLE_U02_SIZE;
                        break;
                    default:
                        break;
                    }
                }else if(chipid == 1){
                    switch (rom_ver) {
                    case 2:
                    case 3:
                        size = AICBT_FW_PATCH_TABLE_U02H_SIZE;
                        break;
                    default:
                        break;
                    }
                }else{
                    ASSERT_ERR(0);
                }
            }
            break;
            dbg("%s size is 0\n",__func__);
            break;
    }
    return size;
}

