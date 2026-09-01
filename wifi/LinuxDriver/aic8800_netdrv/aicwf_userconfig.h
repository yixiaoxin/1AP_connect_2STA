/**
 * aicwf_userconfig.c
 *
 * Copyright (C) AICSemi 2018-2025
 */

#ifndef _AICWF_USERCONFIG_H_
#define _AICWF_USERCONFIG_H_

#include "lmac_msg.h"

s8_l get_txpwr_max(s8_l power);
void set_txpwr_loss_ofst(s8_l value);
void get_userconfig_txpwr_loss(txpwr_loss_conf_t *txpwr_loss);
void get_userconfig_txpwr_lvl_in_fdrv(txpwr_lvl_conf_t *txpwr_lvl);
void get_userconfig_txpwr_lvl_v2_in_fdrv(txpwr_lvl_conf_v2_t *txpwr_lvl_v2);
void get_userconfig_txpwr_lvl_v3_in_fdrv(txpwr_lvl_conf_v3_t *txpwr_lvl_v3);
void get_userconfig_txpwr_lvl_v4_in_fdrv(txpwr_lvl_conf_v4_t *txpwr_lvl_v4);
void get_userconfig_txpwr_lvl_adj_in_fdrv(txpwr_lvl_adj_conf_t *txpwr_lvl_adj);

#endif /* _AICWF_USERCONFIG_H_ */

