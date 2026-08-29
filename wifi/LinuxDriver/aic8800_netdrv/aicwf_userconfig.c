/**
 * aicwf_userconfig.c
 *
 * Copyright (C) AICSemi 2018-2025
 */

#include "lmac_msg.h"
#include "rwnx_platform.h"
#include "aicwf_def.h"
#include "aicwf_debug.h"
#include "aicwf_sdio.h"

typedef struct
{
    txpwr_lvl_conf_t txpwr_lvl;
    txpwr_lvl_conf_v2_t txpwr_lvl_v2;
    txpwr_lvl_conf_v3_t txpwr_lvl_v3;
	txpwr_lvl_conf_v4_t txpwr_lvl_v4;
    txpwr_lvl_adj_conf_t txpwr_lvl_adj;
	txpwr_loss_conf_t txpwr_loss;
    txpwr_ofst_conf_t txpwr_ofst;
	txpwr_ofst2x_conf_t txpwr_ofst2x;
	txpwr_ofst2x_conf_v2_t txpwr_ofst2x_v2;
    xtal_cap_conf_t xtal_cap;
} userconfig_info_t;

userconfig_info_t userconfig_info = {
    .txpwr_lvl = {
        .enable           = 1,
        .dsss             = 9,
        .ofdmlowrate_2g4  = 8,
        .ofdm64qam_2g4    = 8,
        .ofdm256qam_2g4   = 8,
        .ofdm1024qam_2g4  = 8,
        .ofdmlowrate_5g   = 11,
        .ofdm64qam_5g     = 10,
        .ofdm256qam_5g    = 9,
        .ofdm1024qam_5g   = 9
    },
    .txpwr_lvl_v2 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 20,   20,   20,   20,   20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16,   15,   15},
    },
    .txpwr_lvl_v3 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 20,   20,   20,   20,   20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16,   15,   15},
         .pwrlvl_11a_5g =
            //NA,   NA,   NA,   NA,   6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 0x80, 0x80, 0x80, 0x80, 20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   15},
        .pwrlvl_11ax_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   15,   14,   14},
    },
    .txpwr_lvl_v4 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 20,   20,   20,   20,   20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16,   15,   15},
        .pwrlvl_11a_5g =
            //6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   15},
        .pwrlvl_11ax_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   15,   14,   14},
    },
	.txpwr_loss = {
		.loss_enable_2g4 = 0,
		.loss_value_2g4 = 0,
		.loss_enable_5g = 0,
		.loss_value_5g = 0,
	},
    .txpwr_ofst = {
        .enable       = 1,
        .chan_1_4     = 0,
        .chan_5_9     = 0,
        .chan_10_13   = 0,
        .chan_36_64   = 0,
        .chan_100_120 = 0,
        .chan_122_140 = 0,
        .chan_142_165 = 0,
    },
    .txpwr_ofst2x = {
        .enable       = 0,
        .pwrofst2x_tbl_2g4 =
        { // ch1-4, ch5-9, ch10-13
            {   0,    0,    0   }, // 11b
            {   0,    0,    0   }, // ofdm_highrate
            {   0,    0,    0   }, // ofdm_lowrate
        },
        .pwrofst2x_tbl_5g =
        { // ch42,  ch58, ch106,ch122,ch138,ch155
            {   0,    0,    0,    0,    0,    0   }, // ofdm_lowrate
            {   0,    0,    0,    0,    0,    0   }, // ofdm_highrate
            {   0,    0,    0,    0,    0,    0   }, // ofdm_midrate
        },
    },
    .txpwr_ofst2x_v2 = {
        .enable        = 0,
        .pwrofst_flags = 0,
        .pwrofst2x_tbl_2g4_ant0 =
        { // 11b, ofdm_highrate, ofdm_lowrate
            {   0,    0,    0   }, // ch1-4
            {   0,    0,    0   }, // ch5-9
            {   0,    0,    0   }, // ch10-13
        },
        .pwrofst2x_tbl_2g4_ant1 =
        { // 11b, ofdm_highrate, ofdm_lowrate
            {   0,    0,    0   }, // ch1-4
            {   0,    0,    0   }, // ch5-9
            {   0,    0,    0   }, // ch10-13
        },
        .pwrofst2x_tbl_5g_ant0 =
        { // ofdm_highrate, ofdm_lowrate, ofdm_midrate
            {   0,    0,    0   }, // ch42
            {   0,    0,    0   }, // ch58
            {   0,    0,    0   }, // ch106
            {   0,    0,    0   }, // ch122
            {   0,    0,    0   }, // ch138
            {   0,    0,    0   }, // ch155
        },
        .pwrofst2x_tbl_5g_ant1 =
        { // ofdm_highrate, ofdm_lowrate, ofdm_midrate
            {   0,    0,    0   }, // ch42
            {   0,    0,    0   }, // ch58
            {   0,    0,    0   }, // ch106
            {   0,    0,    0   }, // ch122
            {   0,    0,    0   }, // ch138
            {   0,    0,    0   }, // ch155
        },
        .pwrofst2x_tbl_6g_ant0 = {   0,   }, // ofdm_highrate: 6e_ch7 ~ 6e_ch229
        .pwrofst2x_tbl_6g_ant1 = {   0,   }, // ofdm_highrate: 6e_ch7 ~ 6e_ch229
    },
    .xtal_cap = {
        .enable        = 0,
        .xtal_cap      = 24,
        .xtal_cap_fine = 31,
    },
};

typedef enum {
	REGIONS_SRRC,
	REGIONS_FCC,
	REGIONS_ETSI,
	REGIONS_JP,
	REGIONS_DEFAULT,
} Regions_code;

typedef struct {
	char ccode[3];
	Regions_code region;
} reg_table;

/* If the region conflicts with the kernel, the actual authentication standard prevails */
reg_table reg_tables[] = {
	{.ccode = "CN", .region = REGIONS_SRRC},
	{.ccode = "US", .region = REGIONS_FCC},
	{.ccode = "DE", .region = REGIONS_ETSI},
	{.ccode = "00", .region = REGIONS_DEFAULT},
	{.ccode = "WW", .region = REGIONS_DEFAULT},
	{.ccode = "XX", .region = REGIONS_DEFAULT},
	{.ccode = "JP", .region = REGIONS_JP},
	{.ccode = "AD", .region = REGIONS_ETSI},
	{.ccode = "AE", .region = REGIONS_ETSI},
	{.ccode = "AF", .region = REGIONS_ETSI},
	{.ccode = "AI", .region = REGIONS_ETSI},
	{.ccode = "AL", .region = REGIONS_ETSI},
	{.ccode = "AM", .region = REGIONS_ETSI},
	{.ccode = "AN", .region = REGIONS_ETSI},
	{.ccode = "AR", .region = REGIONS_FCC},
	{.ccode = "AS", .region = REGIONS_FCC},
	{.ccode = "AT", .region = REGIONS_ETSI},
	{.ccode = "AU", .region = REGIONS_ETSI},
	{.ccode = "AW", .region = REGIONS_ETSI},
	{.ccode = "AZ", .region = REGIONS_ETSI},
	{.ccode = "BA", .region = REGIONS_ETSI},
	{.ccode = "BB", .region = REGIONS_FCC},
	{.ccode = "BD", .region = REGIONS_JP},
	{.ccode = "BE", .region = REGIONS_ETSI},
	{.ccode = "BF", .region = REGIONS_FCC},
	{.ccode = "BG", .region = REGIONS_ETSI},
	{.ccode = "BH", .region = REGIONS_ETSI},
	{.ccode = "BL", .region = REGIONS_ETSI},
	{.ccode = "BM", .region = REGIONS_FCC},
	{.ccode = "BN", .region = REGIONS_JP},
	{.ccode = "BO", .region = REGIONS_JP},
	{.ccode = "BR", .region = REGIONS_FCC},
	{.ccode = "BS", .region = REGIONS_FCC},
	{.ccode = "BT", .region = REGIONS_ETSI},
	{.ccode = "BW", .region = REGIONS_ETSI},
	{.ccode = "BY", .region = REGIONS_ETSI},
	{.ccode = "BZ", .region = REGIONS_JP},
	{.ccode = "CA", .region = REGIONS_FCC},
	{.ccode = "CF", .region = REGIONS_FCC},
	{.ccode = "CH", .region = REGIONS_ETSI},
	{.ccode = "CI", .region = REGIONS_FCC},
	{.ccode = "CL", .region = REGIONS_ETSI},
	{.ccode = "CO", .region = REGIONS_FCC},
	{.ccode = "CR", .region = REGIONS_FCC},
	{.ccode = "CX", .region = REGIONS_FCC},
	{.ccode = "CY", .region = REGIONS_ETSI},
	{.ccode = "CZ", .region = REGIONS_ETSI},
	{.ccode = "DK", .region = REGIONS_ETSI},
	{.ccode = "DM", .region = REGIONS_FCC},
	{.ccode = "DO", .region = REGIONS_FCC},
	{.ccode = "DZ", .region = REGIONS_JP},
	{.ccode = "EC", .region = REGIONS_FCC},
	{.ccode = "EE", .region = REGIONS_ETSI},
	{.ccode = "EG", .region = REGIONS_ETSI},
	{.ccode = "ES", .region = REGIONS_ETSI},
	{.ccode = "ET", .region = REGIONS_ETSI},
	{.ccode = "FI", .region = REGIONS_ETSI},
	{.ccode = "FM", .region = REGIONS_FCC},
	{.ccode = "FR", .region = REGIONS_ETSI},
	{.ccode = "GB", .region = REGIONS_ETSI},
	{.ccode = "GD", .region = REGIONS_FCC},
	{.ccode = "GE", .region = REGIONS_ETSI},
	{.ccode = "GF", .region = REGIONS_ETSI},
	{.ccode = "GH", .region = REGIONS_FCC},
	{.ccode = "GI", .region = REGIONS_ETSI},
	{.ccode = "GL", .region = REGIONS_ETSI},
	{.ccode = "GP", .region = REGIONS_ETSI},
	{.ccode = "GR", .region = REGIONS_ETSI},
	{.ccode = "GT", .region = REGIONS_FCC},
	{.ccode = "GU", .region = REGIONS_FCC},
	{.ccode = "GY", .region = REGIONS_DEFAULT},
	{.ccode = "HK", .region = REGIONS_ETSI},
	{.ccode = "HN", .region = REGIONS_FCC},
	{.ccode = "HR", .region = REGIONS_ETSI},
	{.ccode = "HT", .region = REGIONS_FCC},
	{.ccode = "HU", .region = REGIONS_ETSI},
	{.ccode = "ID", .region = REGIONS_ETSI},
	{.ccode = "IE", .region = REGIONS_ETSI},
	{.ccode = "IL", .region = REGIONS_ETSI},
	{.ccode = "IN", .region = REGIONS_ETSI},
	{.ccode = "IQ", .region = REGIONS_ETSI},
	{.ccode = "IR", .region = REGIONS_JP},
	{.ccode = "IS", .region = REGIONS_ETSI},
	{.ccode = "IT", .region = REGIONS_ETSI},
	{.ccode = "JM", .region = REGIONS_FCC},
	{.ccode = "JO", .region = REGIONS_ETSI},
	{.ccode = "KE", .region = REGIONS_ETSI},
	{.ccode = "KG", .region = REGIONS_ETSI},
	{.ccode = "KH", .region = REGIONS_ETSI},
	{.ccode = "KN", .region = REGIONS_ETSI},
	{.ccode = "KP", .region = REGIONS_JP},
	{.ccode = "KR", .region = REGIONS_ETSI},
	{.ccode = "KW", .region = REGIONS_ETSI},
	{.ccode = "KY", .region = REGIONS_FCC},
	{.ccode = "KZ", .region = REGIONS_DEFAULT},
	{.ccode = "LB", .region = REGIONS_ETSI},
	{.ccode = "LC", .region = REGIONS_ETSI},
	{.ccode = "LI", .region = REGIONS_ETSI},
	{.ccode = "LK", .region = REGIONS_FCC},
	{.ccode = "LS", .region = REGIONS_ETSI},
	{.ccode = "LT", .region = REGIONS_ETSI},
	{.ccode = "LU", .region = REGIONS_ETSI},
	{.ccode = "LV", .region = REGIONS_ETSI},
	{.ccode = "LY", .region = REGIONS_ETSI},
	{.ccode = "MA", .region = REGIONS_ETSI},
	{.ccode = "MC", .region = REGIONS_ETSI},
	{.ccode = "MD", .region = REGIONS_ETSI},
	{.ccode = "ME", .region = REGIONS_ETSI},
	{.ccode = "MF", .region = REGIONS_ETSI},
	{.ccode = "MH", .region = REGIONS_FCC},
	{.ccode = "MK", .region = REGIONS_ETSI},
	{.ccode = "MN", .region = REGIONS_ETSI},
	{.ccode = "MO", .region = REGIONS_ETSI},
	{.ccode = "MP", .region = REGIONS_FCC},
	{.ccode = "MQ", .region = REGIONS_ETSI},
	{.ccode = "MR", .region = REGIONS_ETSI},
	{.ccode = "MT", .region = REGIONS_ETSI},
	{.ccode = "MU", .region = REGIONS_FCC},
	{.ccode = "MV", .region = REGIONS_ETSI},
	{.ccode = "MW", .region = REGIONS_ETSI},
	{.ccode = "MX", .region = REGIONS_FCC},
	{.ccode = "MY", .region = REGIONS_FCC},
	{.ccode = "NA", .region = REGIONS_ETSI},
	{.ccode = "NG", .region = REGIONS_ETSI},
	{.ccode = "NI", .region = REGIONS_FCC},
	{.ccode = "NL", .region = REGIONS_ETSI},
	{.ccode = "NO", .region = REGIONS_ETSI},
	{.ccode = "NP", .region = REGIONS_JP},
	{.ccode = "NZ", .region = REGIONS_ETSI},
	{.ccode = "OM", .region = REGIONS_ETSI},
	{.ccode = "PA", .region = REGIONS_FCC},
	{.ccode = "PE", .region = REGIONS_FCC},
	{.ccode = "PF", .region = REGIONS_ETSI},
	{.ccode = "PG", .region = REGIONS_FCC},
	{.ccode = "PH", .region = REGIONS_FCC},
	{.ccode = "PK", .region = REGIONS_ETSI},
	{.ccode = "PL", .region = REGIONS_ETSI},
	{.ccode = "PM", .region = REGIONS_ETSI},
	{.ccode = "PR", .region = REGIONS_FCC},
	{.ccode = "PT", .region = REGIONS_ETSI},
	{.ccode = "PW", .region = REGIONS_FCC},
	{.ccode = "PY", .region = REGIONS_FCC},
	{.ccode = "QA", .region = REGIONS_ETSI},
	{.ccode = "RE", .region = REGIONS_ETSI},
	{.ccode = "RO", .region = REGIONS_ETSI},
	{.ccode = "RS", .region = REGIONS_ETSI},
	{.ccode = "RU", .region = REGIONS_ETSI},
	{.ccode = "RW", .region = REGIONS_FCC},
	{.ccode = "SA", .region = REGIONS_ETSI},
	{.ccode = "SE", .region = REGIONS_ETSI},
	{.ccode = "SG", .region = REGIONS_ETSI},
	{.ccode = "SI", .region = REGIONS_ETSI},
	{.ccode = "SK", .region = REGIONS_ETSI},
	{.ccode = "SM", .region = REGIONS_ETSI},
	{.ccode = "SN", .region = REGIONS_FCC},
	{.ccode = "SR", .region = REGIONS_ETSI},
	{.ccode = "SV", .region = REGIONS_FCC},
	{.ccode = "SY", .region = REGIONS_DEFAULT},
	{.ccode = "TC", .region = REGIONS_FCC},
	{.ccode = "TD", .region = REGIONS_ETSI},
	{.ccode = "TG", .region = REGIONS_ETSI},
	{.ccode = "TH", .region = REGIONS_FCC},
	{.ccode = "TJ", .region = REGIONS_ETSI},
	{.ccode = "TM", .region = REGIONS_ETSI},
	{.ccode = "TN", .region = REGIONS_ETSI},
	{.ccode = "TR", .region = REGIONS_ETSI},
	{.ccode = "TT", .region = REGIONS_FCC},
	{.ccode = "TW", .region = REGIONS_FCC},
	{.ccode = "UA", .region = REGIONS_ETSI},
	{.ccode = "UG", .region = REGIONS_FCC},
	{.ccode = "UY", .region = REGIONS_FCC},
	{.ccode = "UZ", .region = REGIONS_ETSI},
	{.ccode = "VC", .region = REGIONS_ETSI},
	{.ccode = "VE", .region = REGIONS_FCC},
	{.ccode = "VI", .region = REGIONS_FCC},
	{.ccode = "VN", .region = REGIONS_JP},
	{.ccode = "VU", .region = REGIONS_FCC},
	{.ccode = "WF", .region = REGIONS_ETSI},
	{.ccode = "YE", .region = REGIONS_DEFAULT},
	{.ccode = "YT", .region = REGIONS_ETSI},
	{.ccode = "ZA", .region = REGIONS_ETSI},
	{.ccode = "ZM", .region = REGIONS_ETSI},
	{.ccode = "ZW", .region = REGIONS_ETSI},
};

s8_l get_txpwr_max(s8_l power)
{
	int i=0;

	if (g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800M40 || g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800M80X2){
		for (i = 0; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i];
		}
	    for (i = 0; i <= 9; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i];
	    }
	    for (i = 0; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i];
	    }
		for (i = 4; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i];
		}
	    for (i = 0; i <= 9; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i];
	    }
		for (i = 0; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i])
				power = userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i];
		}

		if ((userconfig_info.txpwr_loss.loss_enable_2g4 == 1) ||
			(userconfig_info.txpwr_loss.loss_enable_5g == 1)) {
			if (userconfig_info.txpwr_loss.loss_value_2g4 <
				userconfig_info.txpwr_loss.loss_value_5g)
				power += userconfig_info.txpwr_loss.loss_value_5g;
			else
				power += userconfig_info.txpwr_loss.loss_value_2g4;
		}

	}else if(g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800MC){
		for (i = 0; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[i])
				power = userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[i];
		}
	    for (i = 0; i <= 9; i++){
			if(power < userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[i])
				power = userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[i];
	    }
	    for (i = 0; i <= 11; i++){
			if(power < userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[i])
				power = userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[i];
	    }
	}

	printk("%s:txpwr_max:%d \r\n",__func__,power);
	return power;
}

void set_txpwr_loss_ofst(s8_l value)
{
	int i=0;
	if (g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800M40){
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i] += value;
		}
	    for (i = 0; i <= 9; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i] += value;
	    }
	    for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i] += value;
	    }
		for (i = 4; i <= 11; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i] += value;
		}
	    for (i = 0; i <= 9; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i] += value;
	    }
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i] += value;
		}
	}else if( g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800M80X2){
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11b_11ag_2g4[i] += value;
		}
		for (i = 0; i <= 9; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11n_11ac_2g4[i] += value;
		}
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11ax_2g4[i] += value;
		}
		for (i = 0; i <= 7; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11a_5g[i] += value;
		}
		for (i = 0; i <= 9; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11n_11ac_5g[i] += value;
		}
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v4.pwrlvl_11ax_5g[i] += value;
		}
	}else if(g_rwnx_plat->sdiodev->chipid == PRODUCT_ID_AIC8800MC){
		for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[i] += value;
		}
	    for (i = 0; i <= 9; i++){
			userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[i] += value;
	    }
	    for (i = 0; i <= 11; i++){
			userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[i] += value;
	    }
	}
	printk("%s:value:%d\r\n", __func__, value);
}

void get_userconfig_txpwr_loss(txpwr_loss_conf_t *txpwr_loss)
{
	txpwr_loss->loss_enable_2g4 = userconfig_info.txpwr_loss.loss_enable_2g4;
	txpwr_loss->loss_value_2g4 = userconfig_info.txpwr_loss.loss_value_2g4;
	txpwr_loss->loss_enable_5g = userconfig_info.txpwr_loss.loss_enable_5g;
	txpwr_loss->loss_value_5g = userconfig_info.txpwr_loss.loss_value_5g;

	AICWFDBG(LOGDEBUG, "%s:loss_enable_2g4: %d, val_2g4: %d, loss_enable_5g: %d, val_5g: %d\r\n", __func__,
				txpwr_loss->loss_enable_2g4, txpwr_loss->loss_value_2g4,
				txpwr_loss->loss_enable_5g, txpwr_loss->loss_value_5g);

}

void get_userconfig_txpwr_lvl_in_fdrv(txpwr_lvl_conf_t *txpwr_lvl)
{
    txpwr_lvl->enable           = userconfig_info.txpwr_lvl.enable;
    txpwr_lvl->dsss             = userconfig_info.txpwr_lvl.dsss;
    txpwr_lvl->ofdmlowrate_2g4  = userconfig_info.txpwr_lvl.ofdmlowrate_2g4;
    txpwr_lvl->ofdm64qam_2g4    = userconfig_info.txpwr_lvl.ofdm64qam_2g4;
    txpwr_lvl->ofdm256qam_2g4   = userconfig_info.txpwr_lvl.ofdm256qam_2g4;
    txpwr_lvl->ofdm1024qam_2g4  = userconfig_info.txpwr_lvl.ofdm1024qam_2g4;
    txpwr_lvl->ofdmlowrate_5g   = userconfig_info.txpwr_lvl.ofdmlowrate_5g;
    txpwr_lvl->ofdm64qam_5g     = userconfig_info.txpwr_lvl.ofdm64qam_5g;
    txpwr_lvl->ofdm256qam_5g    = userconfig_info.txpwr_lvl.ofdm256qam_5g;
    txpwr_lvl->ofdm1024qam_5g   = userconfig_info.txpwr_lvl.ofdm1024qam_5g;

    AICWFDBG(LOGINFO, "%s:enable:%d\r\n",          __func__, txpwr_lvl->enable);
    AICWFDBG(LOGINFO, "%s:dsss:%d\r\n",            __func__, txpwr_lvl->dsss);
    AICWFDBG(LOGINFO, "%s:ofdmlowrate_2g4:%d\r\n", __func__, txpwr_lvl->ofdmlowrate_2g4);
    AICWFDBG(LOGINFO, "%s:ofdm64qam_2g4:%d\r\n",   __func__, txpwr_lvl->ofdm64qam_2g4);
    AICWFDBG(LOGINFO, "%s:ofdm256qam_2g4:%d\r\n",  __func__, txpwr_lvl->ofdm256qam_2g4);
    AICWFDBG(LOGINFO, "%s:ofdm1024qam_2g4:%d\r\n", __func__, txpwr_lvl->ofdm1024qam_2g4);
    AICWFDBG(LOGINFO, "%s:ofdmlowrate_5g:%d\r\n",  __func__, txpwr_lvl->ofdmlowrate_5g);
    AICWFDBG(LOGINFO, "%s:ofdm64qam_5g:%d\r\n",    __func__, txpwr_lvl->ofdm64qam_5g);
    AICWFDBG(LOGINFO, "%s:ofdm256qam_5g:%d\r\n",   __func__, txpwr_lvl->ofdm256qam_5g);
    AICWFDBG(LOGINFO, "%s:ofdm1024qam_5g:%d\r\n",  __func__, txpwr_lvl->ofdm1024qam_5g);
}

void get_userconfig_txpwr_lvl_v2_in_fdrv(txpwr_lvl_conf_v2_t *txpwr_lvl_v2)
{
    *txpwr_lvl_v2 = userconfig_info.txpwr_lvl_v2;

    AICWFDBG(LOGINFO, "%s:enable:%d\r\n",               __func__, txpwr_lvl_v2->enable);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[11]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[11]);
}

void get_userconfig_txpwr_lvl_v3_in_fdrv(txpwr_lvl_conf_v3_t *txpwr_lvl_v3)
{
    *txpwr_lvl_v3 = userconfig_info.txpwr_lvl_v3;

    AICWFDBG(LOGINFO, "%s:enable:%d\r\n",               __func__, txpwr_lvl_v3->enable);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[11]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[11]);

    AICWFDBG(LOGINFO, "%s:lvl_11a_1m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_2m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_5m5_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_11m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_6m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_9m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_12m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_18m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_24m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_36m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_48m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_54m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[11]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_5g:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_5g:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[11]);
}

void get_userconfig_txpwr_lvl_v4_in_fdrv(txpwr_lvl_conf_v4_t *txpwr_lvl_v4)
{
    *txpwr_lvl_v4 = userconfig_info.txpwr_lvl_v4;

    AICWFDBG(LOGINFO, "%s:enable:%d\r\n",               __func__, txpwr_lvl_v4->enable);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[11]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v4->pwrlvl_11ax_2g4[11]);

    AICWFDBG(LOGINFO, "%s:lvl_11a_6m_5g:%d\r\n",        __func__, txpwr_lvl_v4->pwrlvl_11a_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_9m_5g:%d\r\n",        __func__, txpwr_lvl_v4->pwrlvl_11a_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_12m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_18m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_24m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_36m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_48m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11a_54m_5g:%d\r\n",       __func__, txpwr_lvl_v4->pwrlvl_11a_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_5g:%d\r\n", __func__, txpwr_lvl_v4->pwrlvl_11n_11ac_5g[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[5]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[6]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[7]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[8]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_5g:%d\r\n",     __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[9]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_5g:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[10]);
    AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_5g:%d\r\n",    __func__, txpwr_lvl_v4->pwrlvl_11ax_5g[11]);
}

void get_userconfig_txpwr_lvl_adj_in_fdrv(txpwr_lvl_adj_conf_t *txpwr_lvl_adj)
{
    *txpwr_lvl_adj = userconfig_info.txpwr_lvl_adj;

    AICWFDBG(LOGINFO, "%s:enable:%d\r\n",                   __func__, txpwr_lvl_adj->enable);
    AICWFDBG(LOGINFO, "%s:lvl_adj_2g4_chan_1_4:%d\r\n",     __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_2g4[0]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_2g4_chan_5_9:%d\r\n",     __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_2g4[1]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_2g4_chan_10_13:%d\r\n",   __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_2g4[2]);

    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_42:%d\r\n",       __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[0]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_58:%d\r\n",       __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[1]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_106:%d\r\n",      __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[2]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_122:%d\r\n",      __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[3]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_138:%d\r\n",      __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[4]);
    AICWFDBG(LOGINFO, "%s:lvl_adj_5g_chan_155:%d\r\n",      __func__, txpwr_lvl_adj->pwrlvl_adj_tbl_5g[5]);
}

