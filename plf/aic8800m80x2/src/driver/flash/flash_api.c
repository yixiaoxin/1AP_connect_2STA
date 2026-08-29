/**
 ****************************************************************************************
 *
 * @file flash_api.c
 *
 * @brief FLASH API functions
 *
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "flash_api.h"
#include "flash_api_btdm.h"
#include "rom_api.h"
#include "ll.h"
#include "dbg_assert.h"
#include "dbg.h"
#include <string.h>

/* ROM APIs */
unsigned int flash_chip_size_get(void)
{
    unsigned int ret;
    critical_section_start();
    ret = ROM_FlashChipSizeGet();
    critical_section_end();
    return ret;
}

unsigned int flash_chip_id_get(void)
{
    unsigned int ret;
    critical_section_start();
    ret = ROM_FlashChipIdGet();
    critical_section_end();
    return ret;
}

void flash_chip_erase(void)
{
    critical_section_start();
    ROM_FlashChipErase();
    critical_section_end();
}

int flash_erase(void *a4k, unsigned int len)
{
    int ret;
    critical_section_start();
    ret = ROM_FlashErase((unsigned int)a4k, len);
    critical_section_end();
    return ret;
}

int flash_write(void *adr, unsigned int len, void *buf)
{
    int ret;
    critical_section_start();
    ret = ROM_FlashWrite((unsigned int)adr, len, (unsigned int)buf);
    critical_section_end();
    return ret;
}

int flash_read(void *adr, unsigned int len, void *buf)
{
    int ret;
    critical_section_start();
    ret = ROM_FlashRead((unsigned int)adr, len, (unsigned int)buf);
    critical_section_end();
    return ret;
}

void flash_cache_invalid_all(void)
{
    critical_section_start();
    ROM_FlashCacheInvalidAll();
    critical_section_end();
}

void flash_cache_invalid_range(void *adr, unsigned int len)
{
    critical_section_start();
    ROM_FlashCacheInvalidRange((unsigned int)adr, len);
    critical_section_end();
}

unsigned int flash_status_register_read(unsigned int sr2_exist)
{
    unsigned int reg_val;
    critical_section_start();
    reg_val = ROM_FlashStatusRegRead(sr2_exist);
    critical_section_end();
    return reg_val;
}

void flash_status_register_write(unsigned int val, unsigned int msk)
{
    critical_section_start();
    ROM_FlashStatusRegWrite(val, msk);
    critical_section_end();
}

int flash_status_register_check(void)
{
    int ret = 0;
    unsigned int reg_val;
    critical_section_start();
    reg_val = ROM_FlashStatusRegRead(0) & 0x43FC;
    critical_section_end();
    if (0x0200 != reg_val) {
        if (0x4200 == reg_val) {
            ret = 1;
        } else {
            ret = -1;
        }
    }
    return ret;
}

void flash_status_register_recover(void)
{
    critical_section_start();
    ROM_FlashStatusRegWrite(0x0200, 0x43FC);
    critical_section_end();
}

void flash_status_register_protect_set(void)
{
    // enable flash protect(CMP)
    flash_status_register_write(0x4000, 0x4000);
}

/**
 * +=========================+  <--------------- end of application
 * |                         |     ^
 * |         UNUSED          |     |
 * |                         |     v
 * +-------------------------+  <--------------- user_data_flash_addr
 * |                         |     ^
 * |         user_data       |     | user_data_flash_length
 * |                         |     v
 * +=========================+  <--------------- wifi_calib_info0_base
 * |                         |     ^
 * |     wifi_calib_info0    |     |  4KB
 * |                         |     v
 * +=========================+  <--------------- btdm_calib_info_base
 * |                         |     ^
 * |     btdm_calib_info     |     |  4KB
 * |                         |     v
 * +=========================+  <--------------- sys_info_flash_addr(btdm_info_base)
 * |          |              |     ^
 * |          | bt_ble_info  |     |  4KB
 * |          |              |     v
 * |          |--------------+  <--------------- wifi_info_base
 * |          |              |     ^
 * |          | wifi_info    |     |  4KB
 * |          |              |     v
 * | sys_info |--------------+  <--------------- calib_info_base
 * |          |              |     ^
 * |          | calib_info   |     |  4KB : factory calib(RO)
 * |          |              |     v
 * |          |--------------+  <--------------- boot_info_base
 * |          |              |     ^
 * |          | boot_info    |     |  4KB : chip reserved(RO)
 * |          |              |     v
 * +=========================+  <--------------- EOF
 */

/* pre-defined sys info */
#define FLASH_BASE_ADDR             0x04000000//0x08000000
#define FLASH_SECTOR_SIZE           0x00001000
#define FLASH_RESERVED_SECTOR_CNT   (4) // for sys_info
#define FLASH_RFCAL_RES_SECTOR_CNT  (2) // for rf calib res

#define FLASH_BTDM_INFO_OFFSET      (0 * FLASH_SECTOR_SIZE)
#define FLASH_WIFI_INFO_OFFSET      (1 * FLASH_SECTOR_SIZE)
#define FLASH_CALIB_INFO_OFFSET     (2 * FLASH_SECTOR_SIZE)

#define FLASH_BTDM_INFO_MAGIC_NUM   0x4D445442  /* "BTDM" */
#define FLASH_WIFI_INFO_MAGIC_NUM   0x49464957  /* "WIFI" */
#define FLASH_CALIB_INFO_MAGIC_NUM  0x494C4143  /* "CALI" */

#define FLASH_WIFI_CALRES0_OFFSET   (0 * FLASH_SECTOR_SIZE)
#define FLASH_BTDM_CALRES_OFFSET    (1 * FLASH_SECTOR_SIZE)

#define FLASH_WIFI_CALRES0_MAGIC_NUM    0x30524357  /* "WCR0" */
#define FLASH_BTDM_CALRES_MAGIC_NUM     0x52435442  /* "BTCR" */

/* calib info */
#define CALIB_INFO_XTAL_CAP_FLAG_BIT        (0x01UL << 0)
#define CALIB_INFO_WIFI2G4_TXPWR_FLAG_BIT   (0x01UL << 1)
#define CALIB_INFO_WIFI5G_TXPWR_FLAG_BIT    (0x01UL << 2)
#define CALIB_INFO_WIFI2G4_TXGAIN_FLAG_BIT  (0x01UL << 3)
#define CALIB_INFO_WIFI2G4_RXGAIN_FLAG_BIT  (0x01UL << 4)
#define CALIB_INFO_RF_TONE_PWR_FLAG_BIT     (0x01UL << 5)
#define CALIB_INFO_TEMP_LEVEL_FLAG_BIT      (0x01UL << 6)

/* wifi sta info */
#define WIFI_INFO_STA_CONFIG_FLAG_BIT   (0x01UL << 0)
#define WIFI_INFO_STA_MACADDR_FLAG_BIT  (0x01UL << 1)
#define WIFI_INFO_STA_SSIDPW_FLAG_BIT   (0x01UL << 2)

/* wifi ap info */
#define WIFI_INFO_AP_CONFIG_FLAG_BIT    (0x01UL << 8)
#define WIFI_INFO_AP_MACADDR_FLAG_BIT   (0x01UL << 9)
#define WIFI_INFO_AP_SSIDPW_FLAG_BIT    (0x01UL << 10)
#define WIFI_INFO_AP_CHANNEL_FLAG_BIT   (0x01UL << 11)

/* wifi lwip info */
#define WIFI_INFO_LWIP_CONFIG_FLAG_BIT  (0x01UL << 16)
#define WIFI_INFO_LWIP_DHCPS_FLAG_BIT   (0x01UL << 17)
#define WIFI_INFO_LWIP_DNS_FLAG_BIT     (0x01UL << 18)
#define WIFI_INFO_LWIP_SNTP_FLAG_BIT    (0x01UL << 19)

#define WIFI_INFO_FLAG_MASK (WIFI_INFO_STA_CONFIG_FLAG_BIT | WIFI_INFO_STA_MACADDR_FLAG_BIT | WIFI_INFO_STA_SSIDPW_FLAG_BIT \
                            | WIFI_INFO_AP_CONFIG_FLAG_BIT | WIFI_INFO_AP_MACADDR_FLAG_BIT | WIFI_INFO_AP_SSIDPW_FLAG_BIT | WIFI_INFO_AP_CHANNEL_FLAG_BIT \
                            | WIFI_INFO_LWIP_CONFIG_FLAG_BIT | WIFI_INFO_LWIP_DHCPS_FLAG_BIT | WIFI_INFO_LWIP_DNS_FLAG_BIT | WIFI_INFO_LWIP_SNTP_FLAG_BIT)

/* btdm bt info */
#define BTDM_INFO_BT_FACTORY_FLAG_BIT       (0x01UL << 0)
#if PLF_BT_STACK == 1
#define BTDM_INFO_BT_DEV_INFO_FLAG_BIT      (0x01UL << 1)
#else
#define BTDM_INFO_BT_DEV_INFO_FLAG_BIT      (0)
#endif

/* btdm ble info */
#if PLF_BLE_STACK
#define BTDM_INFO_LE_LTK_FLAG_BIT           (0x01UL << 2)
#define BTDM_INFO_LE_LOC_IRK_FLAG_BIT       (0x01UL << 3)
#define BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT (0x01UL << 4)
#define BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT (0x01UL << 5)
#define BTDM_INFO_LE_RAND_ADDR_FLAG_BIT     (0x01UL << 7)
#define BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT    (0x01UL << 8)
#define BTDM_INFO_LE_WHITELIST_FLAG_BIT     (0x01UL << 9)

#define BTDM_INFO_LE_FLAG_MASK  (BTDM_INFO_LE_LTK_FLAG_BIT | BTDM_INFO_LE_LOC_IRK_FLAG_BIT | BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT \
                               | BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT | BTDM_INFO_LE_RAND_ADDR_FLAG_BIT | BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT\
                               | BTDM_INFO_LE_WHITELIST_FLAG_BIT)
#else
#define BTDM_INFO_LE_FLAG_MASK  (0)
#endif //PLF_BLE_STACK


/* wifi rf calib res info */
#define WRCR_INFO_LOGEN_FLAG_BIT            (0x01UL << 0)
#define WRCR_INFO_IPA_FLAG_BIT              (0x01UL << 1)
#define WRCR_INFO_ILNA_FLAG_BIT             (0x01UL << 2)
#define WRCR_INFO_RXDC_FLAG_BIT             (0x01UL << 3)
#define WRCR_INFO_RXQEC_FLAG_BIT            (0x01UL << 4)
#define WRCR_INFO_LOFT_FLAG_BIT             (0x01UL << 5)
#define WRCR_INFO_CAPA_FLAG_BIT             (0x01UL << 6)

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

unsigned int flash_size = 0;
int sys_info_inited = 0;
unsigned int sys_info_flash_addr = 0;

/* btdm pre-read into ram for now */
int btdm_info_inited = 0;
btdm_info_t btdm_info_ram = {0,};
#if PLF_BT_STACK == 1
static void flash_btdm_bt_dev_init(bt_flash_info_t* bt_info);
#endif

void flash_sys_info_init(void)
{
    // read sys info from flash
    if (!flash_size) {
        flash_size = flash_chip_size_get();
    }
    if (!sys_info_flash_addr) {
        sys_info_flash_addr = FLASH_BASE_ADDR + flash_size - FLASH_RESERVED_SECTOR_CNT * FLASH_SECTOR_SIZE;
    }
    sys_info_inited = 1;
}

// calib info is read only
int flash_calib_info_read(unsigned int flag, void *buf, unsigned int len)
{
    unsigned int info_flag_bits;
    calib_info_t calib_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_CALIB_INFO_OFFSET), sizeof(calib_info_t), (void *)&calib_info);
    if (calib_info.magic_num == FLASH_CALIB_INFO_MAGIC_NUM) {
        info_flag_bits = flag & calib_info.info_flag;
        if(info_flag_bits == 0) {
            return INFO_FLAG_INVALID;
        }
        if (info_flag_bits & CALIB_INFO_XTAL_CAP_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.xtal, sizeof(calib_info.xtal));
            index += sizeof(calib_info.xtal);
        }
        if (info_flag_bits & CALIB_INFO_WIFI2G4_TXPWR_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.wifi2g4_txpwr, sizeof(calib_info.wifi2g4_txpwr));
            index += sizeof(calib_info.wifi2g4_txpwr);
        }
        if (info_flag_bits & CALIB_INFO_WIFI5G_TXPWR_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.wifi5g_txpwr, sizeof(calib_info.wifi5g_txpwr));
            index += sizeof(calib_info.wifi5g_txpwr);
        }
        if (info_flag_bits & CALIB_INFO_WIFI2G4_TXGAIN_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.wifi2g4_txgain, sizeof(calib_info.wifi2g4_txgain));
            index += sizeof(calib_info.wifi2g4_txgain);
        }
        if (info_flag_bits & CALIB_INFO_RF_TONE_PWR_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.rf_tone_pwr, sizeof(calib_info.rf_tone_pwr));
            index += sizeof(calib_info.rf_tone_pwr);
        }
        if (info_flag_bits & CALIB_INFO_TEMP_LEVEL_FLAG_BIT) {
            memcpy((char *)buf + index, &calib_info.temp_info, sizeof(calib_info.temp_info));
            index += sizeof(calib_info.temp_info);
        }
        if (index > len) {
            return INFO_LEN_ERR;
        }
    } else {
        return MAGIC_NUM_ERR;
    }
    return INFO_READ_DONE;
}

void flash_calib_info_write(unsigned int flag, const void *buf, unsigned int len)
{
    calib_info_t calib_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_CALIB_INFO_OFFSET), sizeof(calib_info_t), (void *)&calib_info);
    if (calib_info.magic_num != FLASH_CALIB_INFO_MAGIC_NUM) {
        memset(&calib_info, 0xFF, sizeof(calib_info_t));
        calib_info.magic_num = FLASH_CALIB_INFO_MAGIC_NUM;
        calib_info.info_flag = 0;
    }
    calib_info.info_flag |= flag;
    if (flag & CALIB_INFO_XTAL_CAP_FLAG_BIT) {
        memcpy(&calib_info.xtal, (char *)buf + index, sizeof(calib_info.xtal));
        index += sizeof(calib_info.xtal);
    }
    if (flag & CALIB_INFO_WIFI2G4_TXPWR_FLAG_BIT) {
        memcpy(&calib_info.wifi2g4_txpwr, (char *)buf + index, sizeof(calib_info.wifi2g4_txpwr));
        index += sizeof(calib_info.wifi2g4_txpwr);
    }
    if (flag & CALIB_INFO_WIFI5G_TXPWR_FLAG_BIT) {
        memcpy(&calib_info.wifi5g_txpwr, (char *)buf + index, sizeof(calib_info.wifi5g_txpwr));
        index += sizeof(calib_info.wifi5g_txpwr);
    }
    if (flag & CALIB_INFO_WIFI2G4_TXGAIN_FLAG_BIT) {
        memcpy(&calib_info.wifi2g4_txgain, (char *)buf + index, sizeof(calib_info.wifi2g4_txgain));
        index += sizeof(calib_info.wifi2g4_txgain);
    }
    if (flag & CALIB_INFO_RF_TONE_PWR_FLAG_BIT) {
        memcpy(&calib_info.rf_tone_pwr, (char *)buf + index, sizeof(calib_info.rf_tone_pwr));
        index += sizeof(calib_info.rf_tone_pwr);
    }
    if (flag & CALIB_INFO_TEMP_LEVEL_FLAG_BIT) {
        memcpy(&calib_info.temp_info, (char *)buf + index, sizeof(calib_info.temp_info));
        index += sizeof(calib_info.temp_info);
    }
    if (index > len) {
        dbg("flash_calib_info_write: len err\r\n");
    }
    flash_erase((void *)(sys_info_flash_addr + FLASH_CALIB_INFO_OFFSET), sizeof(calib_info_t));
    flash_write((void *)(sys_info_flash_addr + FLASH_CALIB_INFO_OFFSET), sizeof(calib_info_t), &calib_info);
    flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_CALIB_INFO_OFFSET), sizeof(calib_info_t));
}

int flash_calib_xtal_cap_read(xtal_cap_info_t *xtal_cap)
{
    return flash_calib_info_read(CALIB_INFO_XTAL_CAP_FLAG_BIT, (void *)xtal_cap, sizeof(xtal_cap_info_t));
}

void flash_calib_xtal_cap_write(xtal_cap_info_t *xtal_cap)
{
    flash_calib_info_write(CALIB_INFO_XTAL_CAP_FLAG_BIT, (void *)xtal_cap, sizeof(xtal_cap_info_t));
}

int flash_calib_wifi2g4_txpwr_read(wifi2g4_txpwr_info_t *txpwr)
{
    return flash_calib_info_read(CALIB_INFO_WIFI2G4_TXPWR_FLAG_BIT, (void *)txpwr, sizeof(wifi2g4_txpwr_info_t));
}

void flash_calib_wifi2g4_txpwr_write(wifi2g4_txpwr_info_t *txpwr)
{
    flash_calib_info_write(CALIB_INFO_WIFI2G4_TXPWR_FLAG_BIT, (void *)txpwr, sizeof(wifi2g4_txpwr_info_t));
}

int flash_calib_wifi5g_txpwr_read(wifi5g_txpwr_info_t *txpwr)
{
    return flash_calib_info_read(CALIB_INFO_WIFI5G_TXPWR_FLAG_BIT, (void *)txpwr, sizeof(wifi5g_txpwr_info_t));
}

void flash_calib_wifi5g_txpwr_write(wifi5g_txpwr_info_t *txpwr)
{
    flash_calib_info_write(CALIB_INFO_WIFI5G_TXPWR_FLAG_BIT, (void *)txpwr, sizeof(wifi5g_txpwr_info_t));
}

int flash_calib_wifi2g4_txgain_read(wifi2g4_txgain_info_t *txgain)
{
    return flash_calib_info_read(CALIB_INFO_WIFI2G4_TXGAIN_FLAG_BIT, (void *)txgain, sizeof(wifi2g4_txgain_info_t));
}

void flash_calib_wifi2g4_txgain_write(wifi2g4_txgain_info_t *txgain)
{
    flash_calib_info_write(CALIB_INFO_WIFI2G4_TXGAIN_FLAG_BIT, (void *)txgain, sizeof(wifi2g4_txgain_info_t));
}

int flash_calib_rf_tone_pwr_read(rf_tone_pwr_info_t *tone_pwr)
{
    return flash_calib_info_read(CALIB_INFO_RF_TONE_PWR_FLAG_BIT, (void *)tone_pwr, sizeof(rf_tone_pwr_info_t));
}

void flash_calib_rf_tone_pwr_write(rf_tone_pwr_info_t *tone_pwr)
{
    flash_calib_info_write(CALIB_INFO_RF_TONE_PWR_FLAG_BIT, (void *)tone_pwr, sizeof(rf_tone_pwr_info_t));
}

int flash_calib_temp_level_read(calib_temp_level_info_t *temp_info)
{
    return flash_calib_info_read(CALIB_INFO_TEMP_LEVEL_FLAG_BIT, (void *)temp_info, sizeof(calib_temp_level_info_t));
}

void flash_calib_temp_level_write(calib_temp_level_info_t *temp_info)
{
    flash_calib_info_write(CALIB_INFO_TEMP_LEVEL_FLAG_BIT, (void *)temp_info, sizeof(calib_temp_level_info_t));
}

int flash_wifi_info_read(unsigned int flag, void *buf, unsigned int len)
{
    unsigned int info_flag_bits;
    wifi_info_t wifi_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t), (void *)&wifi_info);
    if (wifi_info.magic_num == FLASH_WIFI_INFO_MAGIC_NUM) {
        info_flag_bits = flag & wifi_info.info_flag;
        if(info_flag_bits == 0) {
            return INFO_FLAG_INVALID;
        }
        #if 0
        if (info_flag_bits & WIFI_INFO_STA_CONFIG_FLAG_BIT) {
            //memcpy((char *)buf + index, &wifi_info.sta_info.config, sizeof(wifi_info.sta_info.config));
            *(unsigned int *)((char *)buf + index) = wifi_info.sta_info.config;
            index += sizeof(wifi_info.sta_info.config);
        }
        #endif
        if (info_flag_bits & WIFI_INFO_STA_MACADDR_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.sta_info.mac_addr, sizeof(wifi_info.sta_info.mac_addr));
            index += sizeof(wifi_info.sta_info.mac_addr);
        }
        #if 0
        if (info_flag_bits & WIFI_INFO_STA_SSIDPW_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.sta_info.ssidpw, sizeof(wifi_info.sta_info.ssidpw));
            index += sizeof(wifi_info.sta_info.ssidpw);
        }
        if (info_flag_bits & WIFI_INFO_AP_CONFIG_FLAG_BIT) {
            //memcpy((char *)buf + index, &wifi_info.ap_info.config, sizeof(wifi_info.ap_info.config));
            *(unsigned int *)((char *)buf + index) = wifi_info.ap_info.config;
            index += sizeof(wifi_info.ap_info.config);
        }
        if (info_flag_bits & WIFI_INFO_AP_MACADDR_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.ap_info.mac_addr, sizeof(wifi_info.ap_info.mac_addr));
            index += sizeof(wifi_info.ap_info.mac_addr);
        }
        if (info_flag_bits & WIFI_INFO_AP_SSIDPW_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.ap_info.ssidpw, sizeof(wifi_info.ap_info.ssidpw));
            index += sizeof(wifi_info.ap_info.ssidpw);
        }
        if (info_flag_bits & WIFI_INFO_AP_CHANNEL_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.ap_info.channel, sizeof(wifi_info.ap_info.channel));
            index += sizeof(wifi_info.ap_info.channel);
        }
        if (info_flag_bits & WIFI_INFO_LWIP_CONFIG_FLAG_BIT) {
            //memcpy((char *)buf + index, &wifi_info.lwip_info.config, sizeof(wifi_info.lwip_info.config));
            *(unsigned int *)((char *)buf + index) = wifi_info.lwip_info.config;
            index += sizeof(wifi_info.lwip_info.config);
        }
        if (info_flag_bits & WIFI_INFO_LWIP_DHCPS_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.lwip_info.dhcps, sizeof(wifi_info.lwip_info.dhcps));
            index += sizeof(wifi_info.lwip_info.dhcps);
        }
        if (info_flag_bits & WIFI_INFO_LWIP_DNS_FLAG_BIT) {
            //memcpy((char *)buf + index, &wifi_info.lwip_info.dns_server, sizeof(wifi_info.lwip_info.dns_server));
            *(unsigned int *)((char *)buf + index) = wifi_info.lwip_info.dns_server;
            index += sizeof(wifi_info.lwip_info.dns_server);
        }
        if (info_flag_bits & WIFI_INFO_LWIP_SNTP_FLAG_BIT) {
            memcpy((char *)buf + index, &wifi_info.lwip_info.sntp, sizeof(wifi_info.lwip_info.sntp));
            index += sizeof(wifi_info.lwip_info.sntp);
        }
        #endif
        if (index > len) {
            return INFO_LEN_ERR;
        }
    } else {
        return MAGIC_NUM_ERR;
    }
    return INFO_READ_DONE;
}

void flash_wifi_info_write(unsigned int flag, const void *buf, unsigned int len)
{
    wifi_info_t wifi_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t), (void *)&wifi_info);
    if (wifi_info.magic_num != FLASH_WIFI_INFO_MAGIC_NUM) {
        memset(&wifi_info, 0xFF, sizeof(wifi_info_t));
        wifi_info.magic_num = FLASH_WIFI_INFO_MAGIC_NUM;
        wifi_info.info_flag = 0;
        wifi_info.sta_info.config = 0;
        wifi_info.ap_info.config = 0;
        wifi_info.lwip_info.config = 0;
    }
    wifi_info.info_flag |= flag;
    #if 0
    if (flag & WIFI_INFO_STA_CONFIG_FLAG_BIT) {
        //memcpy(&wifi_info.sta_info.config, (char *)buf + index, sizeof(wifi_info.sta_info.config));
        wifi_info.sta_info.config = *(unsigned int *)((char *)buf + index);
        index += sizeof(wifi_info.sta_info.config);
    }
    #endif
    if (flag & WIFI_INFO_STA_MACADDR_FLAG_BIT) {
        memcpy(&wifi_info.sta_info.mac_addr, (char *)buf + index, sizeof(wifi_info.sta_info.mac_addr));
        index += sizeof(wifi_info.sta_info.mac_addr);
    }
    #if 0
    if (flag & WIFI_INFO_STA_SSIDPW_FLAG_BIT) {
        memcpy(&wifi_info.sta_info.ssidpw, (char *)buf + index, sizeof(wifi_info.sta_info.ssidpw));
        index += sizeof(wifi_info.sta_info.ssidpw);
    }
    if (flag & WIFI_INFO_AP_CONFIG_FLAG_BIT) {
        //memcpy(&wifi_info.ap_info.config, (char *)buf + index, sizeof(wifi_info.ap_info.config));
        wifi_info.ap_info.config = *(unsigned int *)((char *)buf + index);
        index += sizeof(wifi_info.ap_info.config);
    }
    if (flag & WIFI_INFO_AP_MACADDR_FLAG_BIT) {
        memcpy(&wifi_info.ap_info.mac_addr, (char *)buf + index, sizeof(wifi_info.ap_info.mac_addr));
        index += sizeof(wifi_info.ap_info.mac_addr);
    }
    if (flag & WIFI_INFO_AP_SSIDPW_FLAG_BIT) {
        memcpy(&wifi_info.ap_info.ssidpw, (char *)buf + index, sizeof(wifi_info.ap_info.ssidpw));
        index += sizeof(wifi_info.ap_info.ssidpw);
    }
    if (flag & WIFI_INFO_AP_CHANNEL_FLAG_BIT) {
        memcpy(&wifi_info.ap_info.channel, (char *)buf + index, sizeof(wifi_info.ap_info.channel));
        index += sizeof(wifi_info.ap_info.channel);
    }
    if (flag & WIFI_INFO_LWIP_CONFIG_FLAG_BIT) {
        //memcpy(&wifi_info.lwip_info.config, (char *)buf + index, sizeof(wifi_info.lwip_info.config));
        wifi_info.lwip_info.config = *(unsigned int *)((char *)buf + index);
        index += sizeof(wifi_info.lwip_info.config);
    }
    if (flag & WIFI_INFO_LWIP_DHCPS_FLAG_BIT) {
        memcpy(&wifi_info.lwip_info.dhcps, (char *)buf + index, sizeof(wifi_info.lwip_info.dhcps));
        index += sizeof(wifi_info.lwip_info.dhcps);
    }
    if (flag & WIFI_INFO_LWIP_DNS_FLAG_BIT) {
        //memcpy(&wifi_info.lwip_info.dns_server, (char *)buf + index, sizeof(wifi_info.lwip_info.dns_server));
        wifi_info.lwip_info.dns_server = *(unsigned int *)((char *)buf + index);
        index += sizeof(wifi_info.lwip_info.dns_server);
    }
    if (flag & WIFI_INFO_LWIP_SNTP_FLAG_BIT) {
        memcpy(&wifi_info.lwip_info.sntp, (char *)buf + index, sizeof(wifi_info.lwip_info.sntp));
        index += sizeof(wifi_info.lwip_info.sntp);
    }
    #endif
    if (index > len) {
        dbg("flash_wifi_info_write: len err\r\n");
    }
    flash_erase((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t));
    flash_write((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t), &wifi_info);
    flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t));
}

#if 0
void flash_wifi_info_remove(unsigned int flag)
{
    unsigned int info_flag_bits;
    wifi_info_t wifi_info;
    int need_update = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t), (void *)&wifi_info);
    if (wifi_info.magic_num != FLASH_WIFI_INFO_MAGIC_NUM) {
        memset(&wifi_info, 0xFF, sizeof(wifi_info_t));
        wifi_info.magic_num = FLASH_WIFI_INFO_MAGIC_NUM;
        wifi_info.info_flag = 0;
        wifi_info.sta_info.config = 0;
        wifi_info.ap_info.config = 0;
        wifi_info.lwip_info.config = 0;
        need_update = 1;
    } else {
        info_flag_bits = flag & wifi_info.info_flag;
        if (info_flag_bits) {
            need_update = 1;
            wifi_info.info_flag &= ~info_flag_bits;
            if (info_flag_bits & WIFI_INFO_STA_CONFIG_FLAG_BIT) {
                wifi_info.sta_info.config = 0;
            }
            if (info_flag_bits & WIFI_INFO_STA_MACADDR_FLAG_BIT) {
                memset(&wifi_info.sta_info.mac_addr, 0xFF, sizeof(wifi_info.sta_info.mac_addr));
            }
            if (info_flag_bits & WIFI_INFO_STA_SSIDPW_FLAG_BIT) {
                memset(&wifi_info.sta_info.ssidpw, 0xFF, sizeof(wifi_info.sta_info.ssidpw));
            }
            if (info_flag_bits & WIFI_INFO_AP_CONFIG_FLAG_BIT) {
                wifi_info.ap_info.config = 0;
            }
            if (info_flag_bits & WIFI_INFO_AP_MACADDR_FLAG_BIT) {
                memset(&wifi_info.ap_info.mac_addr, 0xFF, sizeof(wifi_info.ap_info.mac_addr));
            }
            if (info_flag_bits & WIFI_INFO_AP_SSIDPW_FLAG_BIT) {
                memset(&wifi_info.ap_info.ssidpw, 0xFF, sizeof(wifi_info.ap_info.ssidpw));
            }
            if (info_flag_bits & WIFI_INFO_AP_CHANNEL_FLAG_BIT) {
                memset(&wifi_info.ap_info.channel, 0xFF, sizeof(wifi_info.ap_info.channel));
            }
            if (info_flag_bits & WIFI_INFO_LWIP_CONFIG_FLAG_BIT) {
                wifi_info.lwip_info.config = 0;
            }
            if (info_flag_bits & WIFI_INFO_LWIP_DHCPS_FLAG_BIT) {
                memset(&wifi_info.lwip_info.dhcps, 0xFF, sizeof(wifi_info.lwip_info.dhcps));
            }
            if (info_flag_bits & WIFI_INFO_LWIP_DNS_FLAG_BIT) {
                memset(&wifi_info.lwip_info.dns_server, 0xFF, sizeof(wifi_info.lwip_info.dns_server));
            }
            if (info_flag_bits & WIFI_INFO_LWIP_SNTP_FLAG_BIT) {
                memset(&wifi_info.lwip_info.sntp, 0xFF, sizeof(wifi_info.lwip_info.sntp));
            }
        }
    }
    if (need_update) {
        flash_erase((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t));
        flash_write((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t), &wifi_info);
        flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_WIFI_INFO_OFFSET), sizeof(wifi_info_t));
    }
}

int flash_wifi_sta_config_read(unsigned int *config)
{
    return flash_wifi_info_read(WIFI_INFO_STA_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}

void flash_wifi_sta_config_write(unsigned int *config)
{
    flash_wifi_info_write(WIFI_INFO_STA_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}
#endif

int flash_wifi_sta_macaddr_read(unsigned char *addr)
{
    return flash_wifi_info_read(WIFI_INFO_STA_MACADDR_FLAG_BIT, (void *)addr, 6);
}

void flash_wifi_sta_macaddr_write(unsigned char *addr)
{
    flash_wifi_info_write(WIFI_INFO_STA_MACADDR_FLAG_BIT, (void *)addr, 6);
}

#if 0
int flash_wifi_sta_ssidpw_read(char *ssid, char *pass)
{
    wifi_ssidpw_t ssid_pw;
    int ret = flash_wifi_info_read(WIFI_INFO_STA_SSIDPW_FLAG_BIT, (void *)&ssid_pw, sizeof(wifi_ssidpw_t));
    if (!ret) {
        memcpy(ssid, ssid_pw.ssid, min((strlen(ssid_pw.ssid) + 1), sizeof(ssid_pw.ssid)));
        if (ssid_pw.passwd[0] != '\0') {
            memcpy(pass, ssid_pw.passwd, min((strlen(ssid_pw.passwd) + 1), sizeof(ssid_pw.passwd)));
        } else {
            pass[0] = '\0';
        }
    }
    return ret;
}

void flash_wifi_sta_ssidpw_write(char *ssid, char *pass)
{
    wifi_ssidpw_t ssid_pw = {{'\0',},{'\0',}};
    memcpy(ssid_pw.ssid, ssid, min((strlen(ssid) + 1), sizeof(ssid_pw.ssid)));
    if (pass[0] != '\0') {
        memcpy(ssid_pw.passwd, pass, min((strlen(pass) + 1), sizeof(ssid_pw.passwd)));
    }
    flash_wifi_info_write(WIFI_INFO_STA_SSIDPW_FLAG_BIT, (void *)&ssid_pw, sizeof(wifi_ssidpw_t));
}

int flash_wifi_ap_config_read(unsigned int *config)
{
    return flash_wifi_info_read(WIFI_INFO_AP_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}

void flash_wifi_ap_config_write(unsigned int *config)
{
    flash_wifi_info_write(WIFI_INFO_AP_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}

int flash_wifi_ap_macaddr_read(unsigned char *addr)
{
    return flash_wifi_info_read(WIFI_INFO_AP_MACADDR_FLAG_BIT, (void *)addr, 6);
}

void flash_wifi_ap_macaddr_write(unsigned char *addr)
{
    flash_wifi_info_write(WIFI_INFO_AP_MACADDR_FLAG_BIT, (void *)addr, 6);
}

int flash_wifi_ap_ssidpw_read(char *ssid, char *pass)
{
    wifi_ssidpw_t ssid_pw;
    int ret = flash_wifi_info_read(WIFI_INFO_AP_SSIDPW_FLAG_BIT, (void *)&ssid_pw, sizeof(wifi_ssidpw_t));
    if (!ret) {
        memcpy(ssid, ssid_pw.ssid, min((strlen(ssid_pw.ssid) + 1), sizeof(ssid_pw.ssid)));
        if (ssid_pw.passwd[0] != '\0') {
            memcpy(pass, ssid_pw.passwd, min((strlen(ssid_pw.passwd) + 1), sizeof(ssid_pw.passwd)));
        } else {
            pass[0] = '\0';
        }
    }
    return ret;
}

void flash_wifi_ap_ssidpw_write(char *ssid, char *pass)
{
    wifi_ssidpw_t ssid_pw = {{'\0',},{'\0',}};
    memcpy(ssid_pw.ssid, ssid, min((strlen(ssid) + 1), sizeof(ssid_pw.ssid)));
    if (pass[0] != '\0') {
        memcpy(ssid_pw.passwd, pass, min((strlen(pass) + 1), sizeof(ssid_pw.passwd)));
    }
    flash_wifi_info_write(WIFI_INFO_AP_SSIDPW_FLAG_BIT, (void *)&ssid_pw, sizeof(wifi_ssidpw_t));
}

int flash_wifi_ap_channel_read(unsigned int *channel)
{
    return flash_wifi_info_read(WIFI_INFO_AP_CHANNEL_FLAG_BIT, (void *)channel, sizeof(unsigned int));
}

void flash_wifi_ap_channel_write(unsigned int channel)
{
    flash_wifi_info_write(WIFI_INFO_AP_CHANNEL_FLAG_BIT, (void *)&channel, sizeof(unsigned int));
}

int flash_wifi_lwip_config_read(unsigned int *config)
{
    return flash_wifi_info_read(WIFI_INFO_LWIP_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}

void flash_wifi_lwip_config_write(unsigned int *config)
{
    flash_wifi_info_write(WIFI_INFO_LWIP_CONFIG_FLAG_BIT, (void *)config, sizeof(unsigned int));
}

int flash_wifi_lwip_dhcps_read(unsigned int *lease_time, unsigned int *dhcp_start, unsigned int *dhcp_end)
{
    wifi_lwip_dhcps_t dhcps;
    int ret = flash_wifi_info_read(WIFI_INFO_LWIP_DHCPS_FLAG_BIT, (void *)&dhcps, sizeof(wifi_lwip_dhcps_t));
    if (!ret) {
        *lease_time = dhcps.lease_time;
        *dhcp_start = dhcps.dhcp_start;
        *dhcp_end = dhcps.dhcp_end;
    }
    return ret;
}

void flash_wifi_lwip_dhcps_write(unsigned int lease_time, unsigned int dhcp_start, unsigned int dhcp_end)
{
    wifi_lwip_dhcps_t dhcps;
    dhcps.lease_time = lease_time;
    dhcps.dhcp_start = dhcp_start;
    dhcps.dhcp_end = dhcp_end;
    flash_wifi_info_write(WIFI_INFO_LWIP_DHCPS_FLAG_BIT, (void *)&dhcps, sizeof(wifi_lwip_dhcps_t));
}

int flash_wifi_lwip_dnsserv_read(unsigned int *dns_server)
{
    return flash_wifi_info_read(WIFI_INFO_LWIP_DNS_FLAG_BIT, (void *)dns_server, sizeof(unsigned int));
}

void flash_wifi_lwip_dnsserv_write(unsigned int dns_server)
{
    flash_wifi_info_write(WIFI_INFO_LWIP_DNS_FLAG_BIT, (void *)&dns_server, sizeof(unsigned int));
}

int flash_wifi_lwip_sntp_read(signed int *time_zone)
{
    wifi_lwip_sntp_t sntp;
    int ret = flash_wifi_info_read(WIFI_INFO_LWIP_SNTP_FLAG_BIT, (void *)&sntp, sizeof(wifi_lwip_sntp_t));
    if (!ret) {
        *time_zone = sntp.time_zone;
    }
    return ret;
}

void flash_wifi_lwip_sntp_write(signed int time_zone)
{
    wifi_lwip_sntp_t sntp;
    sntp.time_zone = time_zone;
    flash_wifi_info_write(WIFI_INFO_LWIP_SNTP_FLAG_BIT, (void *)&sntp, sizeof(wifi_lwip_sntp_t));
}

void flash_wifi_info_remove_all(void)
{
    flash_wifi_info_remove(WIFI_INFO_FLAG_MASK);
}

unsigned int flash_strlen(unsigned char *str, unsigned char max_len)
{
    unsigned char len =0;
    while(*str != 0xFF){
        if(*str == 0x0)
            break;
        str++;
        len++;
        if(len == max_len)
            break;
    }
    return len;
}
#endif

#if 0
// wifi rf calib result info0 read
int flash_wifi_rfcal_res_info0_read(unsigned int flag, void *buf, unsigned int len)
{
    unsigned int info_flag_bits;
    wf_rf_calib_res_info0_t *p_calres_info_fls;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    p_calres_info_fls = (wf_rf_calib_res_info0_t *)(sys_info_flash_addr
        - (FLASH_RFCAL_RES_SECTOR_CNT * FLASH_SECTOR_SIZE)
        + FLASH_WIFI_CALRES0_OFFSET);
    if (p_calres_info_fls->magic_num == FLASH_WIFI_CALRES0_MAGIC_NUM) {
        info_flag_bits = flag & p_calres_info_fls->info_flag;
        if (info_flag_bits == 0) {
            return INFO_FLAG_INVALID;
        }
        if (info_flag_bits & WRCR_INFO_LOGEN_FLAG_BIT) {
            memcpy((char *)buf + index, &p_calres_info_fls->logen_res, sizeof(wrcr_logen_info_t));
            index += sizeof(wrcr_logen_info_t);
        }
        if (info_flag_bits & WRCR_INFO_IPA_FLAG_BIT) {
            memcpy((char *)buf + index, &p_calres_info_fls->ipa_res, sizeof(wrcr_ipa_info_t));
            index += sizeof(wrcr_ipa_info_t);
        }
        if (info_flag_bits & WRCR_INFO_ILNA_FLAG_BIT) {
            memcpy((char *)buf + index, &p_calres_info_fls->ilna_res, sizeof(wrcr_ilna_info_t));
            index += sizeof(wrcr_ilna_info_t);
        }
        if (index > len) {
            return INFO_LEN_ERR;
        }
    }
    else {
        return MAGIC_NUM_ERR;
    }
    return INFO_READ_DONE;
}

void flash_wifi_rfcal_res_info0_write(unsigned int flag, const void *buf, unsigned int len)
{
    wf_rf_calib_res_info0_t *p_calres_info_fls;
    wf_rf_calib_res_info0_t calres_info_ram;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    p_calres_info_fls = (wf_rf_calib_res_info0_t *)(sys_info_flash_addr
        - (FLASH_RFCAL_RES_SECTOR_CNT * FLASH_SECTOR_SIZE)
        + FLASH_WIFI_CALRES0_OFFSET);
    flash_read((void *)p_calres_info_fls, sizeof(wf_rf_calib_res_info0_t), (void *)&calres_info_ram);
    if (calres_info_ram.magic_num != FLASH_CALIB_INFO_MAGIC_NUM) {
        memset(&calres_info_ram, 0xFF, sizeof(wf_rf_calib_res_info0_t));
        calres_info_ram.magic_num = FLASH_CALIB_INFO_MAGIC_NUM;
        calres_info_ram.info_flag = 0;
    }
    calres_info_ram.info_flag |= flag;
    if (flag & WRCR_INFO_LOGEN_FLAG_BIT) {
        memcpy(&calres_info_ram.logen_res, (char *)buf + index, sizeof(wrcr_logen_info_t));
        index += sizeof(wrcr_logen_info_t);
    }
    if (flag & WRCR_INFO_IPA_FLAG_BIT) {
        memcpy(&calres_info_ram.ipa_res, (char *)buf + index, sizeof(wrcr_ipa_info_t));
        index += sizeof(wrcr_ipa_info_t);
    }
    if (flag & WRCR_INFO_ILNA_FLAG_BIT) {
        memcpy(&calres_info_ram.ilna_res, (char *)buf + index, sizeof(wrcr_ilna_info_t));
        index += sizeof(wrcr_ilna_info_t);
    }
    if (index > len) {
        dbg("flash_wrcr_info0_write: len err\n");
    }
    flash_erase((void *)p_calres_info_fls, sizeof(wf_rf_calib_res_info0_t));
    flash_write((void *)p_calres_info_fls, sizeof(wf_rf_calib_res_info0_t), &calres_info_ram);
    flash_cache_invalid_range((void *)p_calres_info_fls, sizeof(wf_rf_calib_res_info0_t));
}

int flash_wrcr_logen_res_read(wrcr_logen_info_t *logen_res)
{
    return flash_wifi_rfcal_res_info0_read(WRCR_INFO_LOGEN_FLAG_BIT, (void *)logen_res, sizeof(wrcr_logen_info_t));
}

void flash_wrcr_logen_res_write(wrcr_logen_info_t *logen_res)
{
    flash_wifi_rfcal_res_info0_write(WRCR_INFO_LOGEN_FLAG_BIT, (void *)logen_res, sizeof(wrcr_logen_info_t));
}

int flash_wrcr_ipa_res_read(wrcr_ipa_info_t *ipa_res)
{
    return flash_wifi_rfcal_res_info0_read(WRCR_INFO_IPA_FLAG_BIT, (void *)ipa_res, sizeof(wrcr_ipa_info_t));
}

void flash_wrcr_ipa_res_write(wrcr_ipa_info_t *ipa_res)
{
    flash_wifi_rfcal_res_info0_write(WRCR_INFO_IPA_FLAG_BIT, (void *)ipa_res, sizeof(wrcr_ipa_info_t));
}

int flash_wrcr_ilna_res_read(wrcr_ilna_info_t *ilna_res)
{
    return flash_wifi_rfcal_res_info0_read(WRCR_INFO_ILNA_FLAG_BIT, (void *)ilna_res, sizeof(wrcr_ilna_info_t));
}

void flash_wrcr_ilna_res_write(wrcr_ilna_info_t *ilna_res)
{
    flash_wifi_rfcal_res_info0_write(WRCR_INFO_ILNA_FLAG_BIT, (void *)ilna_res, sizeof(wrcr_ilna_info_t));
}
#endif
void flash_btdm_info_init(void)
{
    flash_read((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), &btdm_info_ram);
    if (btdm_info_ram.magic_num != FLASH_BTDM_INFO_MAGIC_NUM) {
        memset(&btdm_info_ram, 0xFF, sizeof(btdm_info_t));
        btdm_info_ram.magic_num = FLASH_BTDM_INFO_MAGIC_NUM;
        btdm_info_ram.info_flag = 0;
    }
    #if PLF_BT_STACK == 1
    flash_btdm_bt_dev_init(&btdm_info_ram.bt_info);
    #endif
    btdm_info_inited = 1;
}

int flash_btdm_info_read(unsigned int flag, void *buf, unsigned int len)
{
    unsigned int info_flag_mux;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    if (!btdm_info_inited) {
        flash_btdm_info_init();
    }
    if (btdm_info_ram.magic_num == FLASH_BTDM_INFO_MAGIC_NUM) {
        info_flag_mux = flag & btdm_info_ram.info_flag;
        if(info_flag_mux == 0) {
            return INFO_FLAG_INVALID;
        }
        if (info_flag_mux & BTDM_INFO_BT_FACTORY_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.bt_factory, sizeof(bt_factory_info_t));
            index += sizeof(bt_factory_info_t);
        }
        #if PLF_BT_STACK
        if (info_flag_mux & BTDM_INFO_BT_DEV_INFO_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.bt_info, sizeof(bt_flash_info_t));
            index += sizeof(bt_flash_info_t);
        }
        #endif
        #if PLF_BLE_STACK
        if (info_flag_mux & BTDM_INFO_LE_LTK_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.le_ltk, sizeof(gapc_ltk_t));
            index += sizeof(gapc_ltk_t);
        }
        if (info_flag_mux & BTDM_INFO_LE_LOC_IRK_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.loc_irk, LOC_KEY_LEN);
            index += LOC_KEY_LEN;
        }
        if (info_flag_mux & BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.bond_status, sizeof(uint8_t));
            index += sizeof(uint8_t);
        }
        if (info_flag_mux & BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.ntf_cfg, sizeof(uint16_t));
            index += sizeof(uint16_t);
        }
        if (info_flag_mux & BTDM_INFO_LE_RAND_ADDR_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.ble_rand_add, sizeof(struct bd_addr));
            index += sizeof(struct bd_addr);
        }
        if (info_flag_mux & BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.ad_filter, sizeof(ble_ad_filter_t));
            index += sizeof(ble_ad_filter_t);
        }
        if (info_flag_mux & BTDM_INFO_LE_WHITELIST_FLAG_BIT) {
            memcpy(buf, &btdm_info_ram.ble_info.wl, sizeof(white_list_t));
            index += sizeof(white_list_t);
        }
        #endif //PLF_BLE_STACK
        if (index > len) {
            return INFO_LEN_ERR;
        }
    }
    else {
        return MAGIC_NUM_ERR;
    }
    return INFO_READ_DONE;
}

void flash_btdm_info_write(unsigned int flag, const void *buf, unsigned int len)
{
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    if (!btdm_info_inited) {
        flash_btdm_info_init();
    }
    if (btdm_info_ram.magic_num != FLASH_BTDM_INFO_MAGIC_NUM) {
        memset(&btdm_info_ram, 0xFF, sizeof(btdm_info_t));
        btdm_info_ram.magic_num = FLASH_BTDM_INFO_MAGIC_NUM;
        btdm_info_ram.info_flag = 0;
    }
    btdm_info_ram.info_flag |= flag;
    if (flag & BTDM_INFO_BT_FACTORY_FLAG_BIT) {
        memcpy(&btdm_info_ram.bt_factory, buf, sizeof(bt_factory_info_t));
        index += sizeof(bt_factory_info_t);
    }
    #if PLF_BT_STACK
    if (flag & BTDM_INFO_BT_DEV_INFO_FLAG_BIT) {
        memcpy(&btdm_info_ram.bt_info, buf, sizeof(bt_flash_info_t));
        index += sizeof(bt_flash_info_t);
    }
    #endif
    #if PLF_BLE_STACK
    if (flag & BTDM_INFO_LE_LTK_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.le_ltk, buf, sizeof(gapc_ltk_t));
        index += sizeof(gapc_ltk_t);
    }
    if (flag & BTDM_INFO_LE_LOC_IRK_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.loc_irk, buf, LOC_KEY_LEN);
        index += sizeof(LOC_KEY_LEN);
    }
    if (flag & BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.bond_status, buf, sizeof(uint8_t));
        index += sizeof(uint8_t);
    }
    if (flag & BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.ntf_cfg, buf, sizeof(uint16_t));
        index += sizeof(uint16_t);
    }
    if (flag & BTDM_INFO_LE_RAND_ADDR_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.ble_rand_add, buf, sizeof(struct bd_addr));
        index += sizeof(struct bd_addr);
    }
    if (flag & BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.ad_filter, buf, sizeof(ble_ad_filter_t));
        index += sizeof(ble_ad_filter_t);
    }
    if (flag & BTDM_INFO_LE_WHITELIST_FLAG_BIT) {
        memcpy(&btdm_info_ram.ble_info.wl, buf, sizeof(white_list_t));
        index += sizeof(white_list_t);
    }
    #endif // PLF_BLE_STACK
    if (index > len) {
        dbg("flash_btdm_info_write: len err\r\n");
    }
    flash_erase((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
    flash_write((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), &btdm_info_ram);
    flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
}

void flash_btdm_info_remove(unsigned int flag)
{
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    if (!btdm_info_inited) {
        flash_btdm_info_init();
    }
    if (btdm_info_ram.info_flag & flag) {
        btdm_info_ram.info_flag &= ~flag;
        if (flag & BTDM_INFO_BT_FACTORY_FLAG_BIT) {
            memset(&btdm_info_ram.bt_factory, 0xFF, sizeof(bt_factory_info_t));
        }
        #if PLF_BT_STACK
        if (flag & BTDM_INFO_BT_DEV_INFO_FLAG_BIT) {
            memset(&btdm_info_ram.bt_info, 0xFF, sizeof(bt_flash_info_t));
        }
        #endif
        #if PLF_BLE_STACK
        uint8_t dflt_addr[BD_ADDR_LEN] = {0xaa,0xc0,0x00,0x45,0x56,0x67};

        if (flag & BTDM_INFO_LE_LTK_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.le_ltk, 0xFF, sizeof(gapc_ltk_t));
        }
        if (flag & BTDM_INFO_LE_LOC_IRK_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.loc_irk, 0xFF, LOC_KEY_LEN);
        }
        if (flag & BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.bond_status, 0xFF, sizeof(uint8_t));
        }
        if (flag & BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.ntf_cfg, 0xFF, sizeof(uint16_t));
        }
        if (flag & BTDM_INFO_LE_RAND_ADDR_FLAG_BIT) {
            memcpy(&btdm_info_ram.ble_info.ble_rand_add[0], &dflt_addr[0], BD_ADDR_LEN);
        }
        if (flag & BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.ad_filter, 0xFF, sizeof(ble_ad_filter_t));
        }
        if (flag & BTDM_INFO_LE_WHITELIST_FLAG_BIT) {
            memset(&btdm_info_ram.ble_info.wl, 0xFF, sizeof(white_list_t));
        }
        #endif // PLF_BLE_STACK
        flash_erase((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
        flash_write((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), &btdm_info_ram);
        flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
    }
}


#if 0
int flash_btdm_info_read(unsigned int flag, void *buf, unsigned int len)
{
    unsigned int info_flag_bits;
    btdm_info_t btdm_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), (void *)&btdm_info);
    if (btdm_info.magic_num == FLASH_BTDM_INFO_MAGIC_NUM) {
        info_flag_bits = flag & btdm_info.info_flag;
        if(info_flag_bits == 0) {
            return INFO_FLAG_INVALID;
        }
        if (info_flag_bits & BTDM_INFO_BT_FACTORY_FLAG_BIT) {
            memcpy(buf, &btdm_info.bt_factory, sizeof(bt_factory_info_t));
            index += sizeof(bt_factory_info_t);
        }
        if (index > len) {
            return INFO_LEN_ERR;
        }
    } else {
        return MAGIC_NUM_ERR;
    }
    return INFO_READ_DONE;
}

void flash_btdm_info_write(unsigned int flag, const void *buf, unsigned int len)
{
    btdm_info_t btdm_info;
    int index = 0;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    flash_read((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), (void *)&btdm_info);
    if (btdm_info.magic_num != FLASH_BTDM_INFO_MAGIC_NUM) {
        memset(&btdm_info, 0xFF, sizeof(btdm_info_t));
        btdm_info.magic_num = FLASH_BTDM_INFO_MAGIC_NUM;
        btdm_info.info_flag = 0;
    }
    btdm_info.info_flag |= flag;
    if (flag & BTDM_INFO_BT_FACTORY_FLAG_BIT) {
        memcpy(&btdm_info.bt_factory, buf, sizeof(bt_factory_info_t));
        index += sizeof(bt_factory_info_t);
    }
    if (index > len) {
        dbg("flash_btdm_info_write: len err\r\n");
    }
    flash_erase((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
    flash_write((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), &btdm_info);
    flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
}
int flash_btdm_bt_factory_read(bt_factory_info_t *bt_fact)
{
    return flash_btdm_info_read(BTDM_INFO_BT_FACTORY_FLAG_BIT, (void *)bt_fact, sizeof(bt_factory_info_t));
}

void flash_btdm_bt_factory_write(bt_factory_info_t *bt_fact)
{
    flash_btdm_info_write(BTDM_INFO_BT_FACTORY_FLAG_BIT, (void *)bt_fact, sizeof(bt_factory_info_t));
}
#else
int flash_btdm_bt_factory_read(void *buf, unsigned int len)
{
    return flash_btdm_info_read(BTDM_INFO_BT_FACTORY_FLAG_BIT, buf, len);
}

void flash_btdm_bt_factory_write(const void *buf, unsigned int len)
{
    flash_btdm_info_write(BTDM_INFO_BT_FACTORY_FLAG_BIT, buf, len);
}
#endif
#if PLF_BT_STACK == 1
static void flash_btdm_bt_dev_init(bt_flash_info_t* bt_info)
{
    dbg("bt_info->init_need = 0x%x\n",bt_info->init_need);
    if(bt_info->init_need != 0xee){
        bt_info->flush_need = 1;
#if APP_SUPPORT_TWS == 1
        bt_info->flash_tws_role = 2;
        bt_info->ble_dev_info.addr_type = 0xff;
        memset(bt_info->ble_dev_info.bleaddr.addr,0,6);
#endif
        bt_info->bt_list.head = NULL;
        bt_info->bt_list.length = 0;
        for(uint8_t i = 0; i < MAX_BD_MUN; i++){
            memset(&bt_info->bt_dev_info[i],0,sizeof(bt_dev_info_t));
            bt_info->bt_dev_info[i].node.prev = NULL;
            bt_info->bt_dev_info[i].node.next = NULL;
            bt_info->bt_dev_info[i].node.data = (void *)&bt_info->bt_dev_info[i];
#ifdef FLASH_BT_TRACE_ON
            dbg("0 bt_dev_info[].node.data = 0x%x\n",bt_info->bt_dev_info[i].node.data);
#endif
        }
        bt_info->init_need = 0xee;
    }else{
        for(uint8_t i = 0; i < MAX_BD_MUN; i++){
            bt_info->bt_dev_info[i].node.data = (void *)&bt_info->bt_dev_info[i];
#ifdef FLASH_BT_TRACE_ON
            dbg("1 bt_dev_info[].node.data = 0x%x ,node = 0x%x\n",bt_info->bt_dev_info[i].node.data,&bt_info->bt_dev_info[i].node);
#endif
        }
    }
#ifdef FLASH_BT_TRACE_ON
    dbg("bt_info->init_need 2 = 0x%x,head = 0x%x,head->data = 0x%x,length = %d\n",bt_info->init_need,bt_info->bt_list.head,bt_info->bt_list.head->data,bt_info->bt_list.length);
#endif
}

bt_flash_info_t *flash_btdm_bt_info_read(unsigned int flag)
{
    bt_flash_info_t * return_info = NULL;
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    if (!btdm_info_inited) {
        flash_btdm_info_init();
    }
    if (btdm_info_ram.magic_num == FLASH_BTDM_INFO_MAGIC_NUM) {
        if (flag & BTDM_INFO_BT_DEV_INFO_FLAG_BIT) {
#ifdef FLASH_BT_TRACE_ON
            dbg("bt_info->init_need = 0x%x,len = %d ,head = 0x%x ,head->data = 0x%x\n",btdm_info_ram.bt_info.init_need,btdm_info_ram.bt_info.bt_list.length,btdm_info_ram.bt_info.bt_list.head,btdm_info_ram.bt_info.bt_list.head->data);
            dbg("node = 0x%x ,node->data = 0x%x\n",&btdm_info_ram.bt_info.bt_dev_info[0].node,btdm_info_ram.bt_info.bt_dev_info[0].node.data);
#endif
            return_info = &btdm_info_ram.bt_info;
        }
    }
    return return_info;
}

void flash_btdm_bt_info_write(unsigned int flag)
{
    if (!sys_info_inited) {
        flash_sys_info_init();
    }
    if (!btdm_info_inited) {
        flash_btdm_info_init();
    }
    if (btdm_info_ram.magic_num != FLASH_BTDM_INFO_MAGIC_NUM) {
        memset(&btdm_info_ram, 0xFF, sizeof(btdm_info_t));
        btdm_info_ram.magic_num = FLASH_BTDM_INFO_MAGIC_NUM;
        btdm_info_ram.info_flag = 0;
    }
    btdm_info_ram.info_flag |= flag;
    if (flag & BTDM_INFO_BT_DEV_INFO_FLAG_BIT) {
        if(btdm_info_ram.bt_info.flush_need == 0) {
            return;
        } else {
            flash_btdm_bt_dev_init(&btdm_info_ram.bt_info);
            btdm_info_ram.bt_info.flush_need = 0;
        }
    }
#ifdef FLASH_BT_TRACE_ON
    dbg("flash_write\n");
#endif
    flash_erase((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
    flash_write((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t), &btdm_info_ram);
    flash_cache_invalid_range((void *)(sys_info_flash_addr + FLASH_BTDM_INFO_OFFSET), sizeof(btdm_info_t));
}

bt_flash_info_t * flash_btdm_bt_dev_read(void)
{
    return flash_btdm_bt_info_read(BTDM_INFO_BT_DEV_INFO_FLAG_BIT);
}

void flash_btdm_bt_dev_write(void)
{
    #if PLF_BLE_ONLY == 0
    flash_btdm_bt_info_write(BTDM_INFO_BT_DEV_INFO_FLAG_BIT);
    #endif
}

#endif
#if PLF_BLE_STACK
int flash_btdm_le_ltk_read(gapc_ltk_t *le_ltk)
{
    return flash_btdm_info_read(BTDM_INFO_LE_LTK_FLAG_BIT, le_ltk, sizeof(gapc_ltk_t));
}

void flash_btdm_le_ltk_write(gapc_ltk_t *le_ltk)
{
    flash_btdm_info_write(BTDM_INFO_LE_LTK_FLAG_BIT, le_ltk, sizeof(gapc_ltk_t));
}

void flash_btdm_le_ltk_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_LTK_FLAG_BIT);
}

int flash_btdm_le_loc_irk_read(uint8_t *loc_irk)
{
    return flash_btdm_info_read(BTDM_INFO_LE_LOC_IRK_FLAG_BIT, loc_irk, sizeof(LOC_KEY_LEN));
}

void flash_btdm_le_loc_irk_write(uint8_t *loc_irk)
{
    flash_btdm_info_write(BTDM_INFO_LE_LOC_IRK_FLAG_BIT, loc_irk, sizeof(LOC_KEY_LEN));
}

void flash_btdm_le_loc_irk_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_LOC_IRK_FLAG_BIT);
}

int flash_btdm_le_periph_bonded_read(uint8_t *bond_status)
{
    return flash_btdm_info_read(BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT, bond_status, sizeof(uint8_t));
}

void flash_btdm_le_periph_bonded_write(uint8_t *bond_status)
{
    flash_btdm_info_write(BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT, bond_status, sizeof(uint8_t));
}

void flash_btdm_le_periph_bonded_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_PERIPH_BONDED_FLAG_BIT);
}

int flash_btdm_le_mouse_ntf_cfg_read(uint8_t *ntf_cfg)
{
    return flash_btdm_info_read(BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT, ntf_cfg, sizeof(uint16_t));
}

void flash_btdm_le_mouse_ntf_cfg_write(uint8_t *ntf_cfg)
{
    flash_btdm_info_write(BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT, ntf_cfg, sizeof(uint16_t));
}

void flash_btdm_le_mouse_ntf_cfg_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_MOUSE_NTF_CFG_FLAG_BIT);
}

void flash_btdm_le_info_remove_all(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_FLAG_MASK);
}

int flash_btdm_le_rand_addr_read(uint8_t *le_addr)
{
    return flash_btdm_info_read(BTDM_INFO_LE_RAND_ADDR_FLAG_BIT, le_addr, sizeof(struct bd_addr));
}

void flash_btdm_le_rand_addr_write(uint8_t *le_addr)
{
    flash_btdm_info_write(BTDM_INFO_LE_RAND_ADDR_FLAG_BIT, le_addr, sizeof(struct bd_addr));
}

void flash_btdm_le_rand_addr_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_RAND_ADDR_FLAG_BIT);
}

int flash_btdm_le_ad_data_ft_read(ble_ad_filter_t *ad_filter)
{
    return flash_btdm_info_read(BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT, ad_filter, sizeof(ble_ad_filter_t));
}

void flash_btdm_le_ad_data_ft_write(ble_ad_filter_t *ad_filter)
{
    flash_btdm_info_write(BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT, ad_filter, sizeof(ble_ad_filter_t));
}

void flash_btdm_le_ad_data_ft_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_AD_DATA_FT_FLAG_BIT);
}

int flash_btdm_le_whitelist_read(white_list_t *wl)
{
    return flash_btdm_info_read(BTDM_INFO_LE_WHITELIST_FLAG_BIT, wl, sizeof(white_list_t));
}

void flash_btdm_le_whitelist_write(white_list_t *wl)
{
    flash_btdm_info_write(BTDM_INFO_LE_WHITELIST_FLAG_BIT, wl, sizeof(white_list_t));
}

void flash_btdm_le_whitelist_remove(void)
{
    flash_btdm_info_remove(BTDM_INFO_LE_WHITELIST_FLAG_BIT);
}

#endif //PLF_BLE_STACK
