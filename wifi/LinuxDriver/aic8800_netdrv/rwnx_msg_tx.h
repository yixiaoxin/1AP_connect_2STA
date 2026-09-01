/**
 ******************************************************************************
 *
 * @file rwnx_msg_tx.c
 *
 * @brief TX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#ifndef _RWNX_MSG_TX_H_
#define _RWNX_MSG_TX_H_

#include "lmac_msg.h"
#include "lmac_types.h"
#include "rwnx_defs.h"

int rwnx_init_cmd_array(void);
int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, u32 mem_data);
int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, struct dbg_mem_read_cfm *cfm);
int rwnx_send_dbg_mem_mask_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, u32 mem_mask, u32 mem_data);
int rwnx_send_rftest_req(struct rwnx_hw *rwnx_hw, u32_l cmd, u32_l argc, u8_l *argv, struct dbg_rftest_cmd_cfm *cfm);
int rwnx_send_txpwr_lvl_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_txpwr_lvl_v3_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_txpwr_lvl_v4_req(struct rwnx_hw *rwnx_hw);


#endif /* _RWNX_MSG_TX_H_ */

