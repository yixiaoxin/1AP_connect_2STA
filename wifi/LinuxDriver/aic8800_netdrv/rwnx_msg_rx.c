/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.c
 *
 * @brief RX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ****************************************************************************************
 */
#include "rwnx_defs.h"
#include "rwnx_cmds.h"
#include "ipc_shared.h"

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {

};

static msg_cb_fct *msg_hdlrs[] = {
    [TASK_DBG]   = dbg_hdlrs,
};

void rwnx_rx_handle_rftest_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);
    //printk("T %d, I %d\r\n", MSG_T(msg->id), MSG_I(msg->id));
    rwnx_hw->cmd_mgr->msgind(rwnx_hw->cmd_mgr, msg,
                            msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}

