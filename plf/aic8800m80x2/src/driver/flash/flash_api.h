#ifndef _FLASH_API_H_
#define _FLASH_API_H_

//#include "rf_calib.h"

/**
 * Macros
 */
#define IS_FLASH_MEM_VALID(addr)    (((unsigned int)(addr) >> 25) == (AIC_FLASH_MEM_BASE >> 25))     // 0x04000000 ~ 0x05FFFFFF
#define IS_CACHE_MEM_VALID(addr)    (((unsigned int)(addr) >> 26) == (AIC_CACHE_MEM_BASE >> 26))     // 0x08000000 ~ 0x0BFFFFFF

/**
 * Structs
 */
/* calib info */
typedef struct {
    unsigned char cap;
    unsigned char cap_fine;
    unsigned char PADDING0[2];
} xtal_cap_info_t;

typedef struct {
    signed char ofst2x_ant0[3][3]; // TXPWR_OFST_BAND_2G4_NUM * TXPWR_OFST_2G4_TYPE_NUM_MAX
    signed char ofst2x_ant1[3][3]; // TXPWR_OFST_BAND_2G4_NUM * TXPWR_OFST_2G4_TYPE_NUM_MAX
    unsigned char PADDING0[2];
} wifi2g4_txpwr_info_t;

typedef struct {
    signed char ofst2x_ant0[6][3]; // TXPWR_OFST_BAND_5G_NUM * TXPWR_OFST_5G_TYPE_NUM_MAX
    signed char ofst2x_ant1[6][3]; // TXPWR_OFST_BAND_5G_NUM * TXPWR_OFST_5G_TYPE_NUM_MAX
    unsigned char PADDING0[2];
} wifi5g_txpwr_info_t;

typedef struct {
    unsigned char pa_drv_ibit;
    unsigned char PADDING0[3];
} wifi2g4_txgain_info_t;

typedef struct {
    unsigned char tone_pwr_2g4;
    unsigned char tone_pwr_5g;
    unsigned char tone_pwr_bt;
    unsigned char PADDING0;
} rf_tone_pwr_info_t;

typedef struct {
    signed char temp_level;
    unsigned char PADDING0[3];
} calib_temp_level_info_t;

typedef struct {
    unsigned int magic_num; /* "CALI" */
    unsigned int info_flag;
    unsigned int RESERVED_08;
    unsigned int RESERVED_0C;
    xtal_cap_info_t xtal;
    wifi2g4_txpwr_info_t wifi2g4_txpwr; // for chan 1~4, 5~9, 10~13
    wifi5g_txpwr_info_t  wifi5g_txpwr;  // for chan 36~64, 100~120, 122~140, 142~165
    wifi2g4_txgain_info_t wifi2g4_txgain;
    unsigned int RESERVED_20; // wifi2g4_rxgain
    rf_tone_pwr_info_t rf_tone_pwr;
    calib_temp_level_info_t temp_info;
} calib_info_t;

/* wifi info */
typedef struct {
    char ssid[48];
    char passwd[64];
} wifi_ssidpw_t;

typedef struct {
    unsigned int  config;
    unsigned char mac_addr[6];
    unsigned char PADDING0[2];
    wifi_ssidpw_t ssidpw;
    //unsigned char pmk[32];
} wifi_sta_info_t;

typedef struct {
    unsigned int  config;
    unsigned char mac_addr[6];
    unsigned char PADDING0[2];
    wifi_ssidpw_t ssidpw;
    unsigned int channel;
} wifi_ap_info_t;

typedef struct {
    unsigned int lease_time;
    unsigned int dhcp_start;
    unsigned int dhcp_end;
} wifi_lwip_dhcps_t;

typedef struct {
    signed int   time_zone;
} wifi_lwip_sntp_t;

typedef struct {
    unsigned int config;
    wifi_lwip_dhcps_t dhcps;
    unsigned int dns_server;
    wifi_lwip_sntp_t sntp;
} wifi_lwip_info_t;

/* wifi sta config */
#define WIFI_STA_CONFIG_STA_AUTO_CONNECT_EN             CO_BIT(0)

/* wifi ap config */
#define WIFI_AP_CONFIG_FORCED_AP_MODE_EN                CO_BIT(0)

/* wifi lwip config */
#define WIFI_LWIP_CONFIG_SNTP_CLIENT_AUTO_CONNECT_EN    CO_BIT(0)

typedef struct {
    unsigned int magic_num; /* "WIFI" */
    unsigned int info_flag;
    unsigned int RESERVED_08;
    unsigned int RESERVED_0C;
    wifi_sta_info_t  sta_info;
    wifi_ap_info_t   ap_info;
    wifi_lwip_info_t lwip_info;
} wifi_info_t;
#if 0
/* btdm info */
typedef struct
{
    unsigned char bt_flash_erased;
    unsigned char factory_mode_setting;
    unsigned char local_bt_addr[6];
    unsigned char local_ble_addr[6];
    unsigned short reserved;
    char local_dev_name[32];
    unsigned char pincode[16];
} bt_factory_info_t;

typedef struct {
    unsigned int magic_num; /* "BTDM" */
    unsigned int info_flag;
    unsigned int RESERVED_08;
    unsigned int RESERVED_0C;
    bt_factory_info_t bt_factory;
} btdm_info_t;
#endif
#if 0
/* wf rf calib result */
typedef struct
{
    uint8_t freq_bit_hb0;
    uint8_t freq_bit_hb1;
    uint8_t reserved0[2];
} wrcr_logen_info_t;

typedef struct
{
    ipa_cal_res_2g4_t ipa_lb0;
    ipa_cal_res_2g4_t ipa_lb1;
    ipa_cal_res_5g_t ipa_hb0;
    ipa_cal_res_5g_t ipa_hb1;
} wrcr_ipa_info_t;

typedef struct
{
    ilna_cal_res_t ilna_lb0;
    ilna_cal_res_t ilna_lb1;
    ilna_cal_res_t ilna_hb0;
    ilna_cal_res_t ilna_hb1;
} wrcr_ilna_info_t;

typedef struct
{
    unsigned int magic_num; /* "WCR0" */
    unsigned int info_flag;
    unsigned int RESERVED_08;
    unsigned int RESERVED_0C;
    wrcr_logen_info_t logen_res;
    wrcr_ipa_info_t ipa_res;
    wrcr_ilna_info_t ilna_res;
} wf_rf_calib_res_info0_t;
#endif

/**
 * Enums
 */
typedef enum {
    INFO_FLAG_INVALID =  1,
    INFO_READ_DONE    =  0,
    MAGIC_NUM_ERR     = -1,
    INFO_LEN_ERR      = -2,
} INFO_READ_STATUS_T;

/**
 * Get chip size as bytes
 */
unsigned int flash_chip_size_get(void);

/**
 * Get chip id
 */
unsigned int flash_chip_id_get(void);

/**
 * Erase all flash except for reserved area
 */
void flash_chip_erase(void);

/**
 * @brief       Erase flash
 * @param[in]   a4k Start address to erase, 4KB aligned
 * @param[in]   len Byte length to erase, 4KB aligned
 * @return      Error code, -4: invalid start address(null or not 4KB aligned)
 *                          -5: zero length
 *                           1: length not 4KB aligned
 */
int flash_erase(void *a4k, unsigned int len);

/**
 * @brief       Write flash
 * @param[in]   adr Start address to write, 256B aligned
 * @param[in]   len Byte length to write
 * @param[in]   buf Buffer address with data writen to flash
 * @return      Error code, -4: invalid start address(null or not 256B aligned)
 *                          -5: zero length
 *                          -6: null buffer address
 */
int flash_write(void *adr, unsigned int len, void *buf);

/**
 * @brief       Read flash
 * @param[in]   adr Start address to write, 4B aligned
 * @param[in]   len Byte length to read
 * @param[out]  buf Buffer address to store data read from flash
 * @return      Error code, -4: invalid start address(null or not 4B aligned)
 *                          -5: zero length
 *                          -6: null buffer address
 */
int flash_read(void *adr, unsigned int len, void *buf);

/**
 * Invalid all flash cache
 */
void flash_cache_invalid_all(void);

/**
 * Invalid the range of flash cache
 */
void flash_cache_invalid_range(void *adr, unsigned int len);

/**
 * @brief  Read flash status register
 * @param sr2_exist Read flash status register-2 or not
 * @return Flash status register values
 */
unsigned int flash_status_register_read(unsigned int sr2_exist);

/**
 * @brief Write flash status register
 * @param val & msk Flash status register value & mask
 */
void flash_status_register_write(unsigned int val, unsigned int msk);

/**
 * @brief  Check flash status register config
 * @return 0:normal, 1:block_protect_en, -1: others
 */
int flash_status_register_check(void);

/**
 * @brief Recover flash status register to normal state
 */
void flash_status_register_recover(void);

/**
 * @brief Set flash protect into status register
 */
void flash_status_register_protect_set(void);


/**
 * calib info api(read only)
 */
int flash_calib_xtal_cap_read(xtal_cap_info_t *xtal_cap);
void flash_calib_xtal_cap_write(xtal_cap_info_t *xtal_cap);
int flash_calib_wifi2g4_txpwr_read(wifi2g4_txpwr_info_t *txpwr);
void flash_calib_wifi2g4_txpwr_write(wifi2g4_txpwr_info_t *txpwr);
int flash_calib_wifi5g_txpwr_read(wifi5g_txpwr_info_t *txpwr);
void flash_calib_wifi5g_txpwr_write(wifi5g_txpwr_info_t *txpwr);
int flash_calib_wifi2g4_txgain_read(wifi2g4_txgain_info_t *txgain);
void flash_calib_wifi2g4_txgain_write(wifi2g4_txgain_info_t *txgain);
int flash_calib_rf_tone_pwr_read(rf_tone_pwr_info_t *tone_pwr);
void flash_calib_rf_tone_pwr_write(rf_tone_pwr_info_t *tone_pwr);
int flash_calib_temp_level_read(calib_temp_level_info_t *temp_info);
void flash_calib_temp_level_write(calib_temp_level_info_t *temp_info);

/**
 * wifi info api
 */
#if 0
int flash_wifi_sta_config_read(unsigned int *config);
void flash_wifi_sta_config_write(unsigned int *config);
#endif
int flash_wifi_sta_macaddr_read(unsigned char *addr);
void flash_wifi_sta_macaddr_write(unsigned char *addr);
#if 0
int flash_wifi_sta_ssidpw_read(char *ssid, char *pass);
void flash_wifi_sta_ssidpw_write(char *ssid, char *pass);
int flash_wifi_ap_config_read(unsigned int *config);
void flash_wifi_ap_config_write(unsigned int *config);
int flash_wifi_ap_macaddr_read(unsigned char *addr);
void flash_wifi_ap_macaddr_write(unsigned char *addr);
int flash_wifi_ap_ssidpw_read(char *ssid, char *pass);
void flash_wifi_ap_ssidpw_write(char *ssid, char *pass);
int flash_wifi_ap_channel_read(unsigned int *channel);
void flash_wifi_ap_channel_write(unsigned int channel);
int flash_wifi_lwip_config_read(unsigned int *config);
void flash_wifi_lwip_config_write(unsigned int *config);
int flash_wifi_lwip_dhcps_read(unsigned int *lease_time, unsigned int *dhcp_start, unsigned int *dhcp_end);
void flash_wifi_lwip_dhcps_write(unsigned int lease_time, unsigned int dhcp_start, unsigned int dhcp_end);
int flash_wifi_lwip_dnsserv_read(unsigned int *dns_server);
void flash_wifi_lwip_dnsserv_write(unsigned int dns_server);
int flash_wifi_lwip_sntp_read(signed int *time_zone);
void flash_wifi_lwip_sntp_write(signed int time_zone);
void flash_wifi_info_remove_all(void);

unsigned int flash_strlen(unsigned char *str,unsigned char max_len);
#endif
#if 0
int flash_btdm_bt_factory_read(bt_factory_info_t *bt_fact);
void flash_btdm_bt_factory_write(bt_factory_info_t *bt_fact);
#endif
#endif /* _FLASH_API_H_ */
