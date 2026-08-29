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
#include "rwnx_msg_tx.h"
#include "rwnx_cmds.h"
#include "rwnx_strs.h"
#include "rwnx_main.h"
#include "aicwf_txrxif.h"
#include "aicwf_userconfig.h"

#define RWNX_CMD_ARRAY_SIZE 40
#define RWNX_CMD_HIGH_WATER_SIZE RWNX_CMD_ARRAY_SIZE/2

struct rwnx_cmd cmd_array[RWNX_CMD_ARRAY_SIZE];
static spinlock_t cmd_array_lock;
int cmd_array_index = 0;

struct rwnx_cmd *rwnx_cmd_malloc(void){
	struct rwnx_cmd *cmd = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&cmd_array_lock, flags);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		if(cmd_array[cmd_array_index].used == 0){
			AICWFDBG(LOGTRACE, "%s get cmd_array[%d]:%p \r\n", __func__, cmd_array_index,&cmd_array[cmd_array_index]);
			cmd = &cmd_array[cmd_array_index];
			cmd_array[cmd_array_index].used = 1;
			break;
		}
	}

	if(cmd_array_index >= RWNX_CMD_HIGH_WATER_SIZE){
		AICWFDBG(LOGERROR, "%s cmd(%d) was pending...\r\n", __func__, cmd_array_index);
		mdelay(100);
	}

	if(!cmd){
		AICWFDBG(LOGERROR, "%s array is empty...\r\n", __func__);
	}

	spin_unlock_irqrestore(&cmd_array_lock, flags);

	return cmd;
}

void rwnx_cmd_free(struct rwnx_cmd *cmd){
	unsigned long flags = 0;

	spin_lock_irqsave(&cmd_array_lock, flags);
	cmd->used = 0;
	AICWFDBG(LOGTRACE, "%s cmd_array[%d]:%p \r\n", __func__, cmd->array_id, cmd);
	spin_unlock_irqrestore(&cmd_array_lock, flags);
}

int rwnx_init_cmd_array(void){

	AICWFDBG(LOGTRACE, "%s Enter \r\n", __func__);
	spin_lock_init(&cmd_array_lock);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		AICWFDBG(LOGTRACE, "%s cmd_queue[%d]:%p \r\n", __func__, cmd_array_index, &cmd_array[cmd_array_index]);
		cmd_array[cmd_array_index].used = 0;
		cmd_array[cmd_array_index].array_id = cmd_array_index;
	}
	AICWFDBG(LOGTRACE, "%s Exit \r\n", __func__);

	return 0;
}

void rwnx_free_cmd_array(void){

	AICWFDBG(LOGTRACE, "%s Enter \r\n", __func__);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		cmd_array[cmd_array_index].used = 0;
	}

	AICWFDBG(LOGTRACE, "%s Exit \r\n", __func__);
}

/**
 ******************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ******************************************************************************
 */
static inline void *rwnx_msg_zalloc(lmac_msg_id_t const id,
									lmac_task_id_t const dest_id,
									lmac_task_id_t const src_id,
									uint16_t const param_len)
{
	struct lmac_msg *msg;
	gfp_t flags;

    #if 0
    if (is_non_blocking_msg(id) && (in_softirq()||in_atomic()))
        flags = GFP_ATOMIC;
    else
        flags = GFP_KERNEL;
    #else
    flags = GFP_ATOMIC;
    #endif

	msg = (struct lmac_msg *)kzalloc(sizeof(struct lmac_msg) + param_len, flags);
	if (msg == NULL) {
		AICWFDBG(LOGERROR, "%s: msg allocation failed\n", __func__);
		return NULL;
	}
	msg->id = id;
	msg->dest_id = dest_id;
	msg->src_id = src_id;
	msg->param_len = param_len;
	//printk("rwnx_msg_zalloc size=%d  id=%d\n",msg->param_len,msg->id);

	return msg->param;
}

static void rwnx_msg_free(struct rwnx_hw *rwnx_hw, const void *msg_params)
{
	struct lmac_msg *msg = container_of((void *)msg_params,
										struct lmac_msg, param);

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	/* Free the message */
	kfree(msg);
}

static int rwnx_send_msg(struct rwnx_hw *rwnx_hw, const void *msg_params,
						 int reqcfm, lmac_msg_id_t reqid, void *cfm)
{
	struct lmac_msg *msg;
	struct rwnx_cmd *cmd;
	bool nonblock;
	int ret = 0;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);
    AICWFDBG(LOGTRACE, "%s (%d)%s reqcfm:%d in_irq:%d in_softirq:%d in_atomic:%d\r\n",
    __func__, reqid, RWNX_ID2STR(reqid), reqcfm, (int)in_irq(), (int)in_softirq(), (int)in_atomic());


#ifdef AICWF_USB_SUPPORT
	if (rwnx_hw->usbdev->state == USB_DOWN_ST) {
		rwnx_msg_free(rwnx_hw, msg_params);
		usb_err("bus is down\n");
		return 0;
	}
#endif
#ifdef AICWF_SDIO_SUPPORT
	if (rwnx_hw->sdiodev->bus_if->state == BUS_DOWN_ST) {
		rwnx_msg_free(rwnx_hw, msg_params);
		sdio_err("bus is down\n");
		return 0;
	}
#endif

	msg = container_of((void *)msg_params, struct lmac_msg, param);

	//nonblock = is_non_blocking_msg(msg->id);
	nonblock = 0;
	cmd = rwnx_cmd_malloc();//kzalloc(sizeof(struct rwnx_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
	cmd->result  = -EINTR;
	cmd->id      = msg->id;
	cmd->reqid   = reqid;
	cmd->a2e_msg = msg;
	cmd->e2a_msg = cfm;
	if (nonblock)
		cmd->flags = RWNX_CMD_FLAG_NONBLOCK;
	if (reqcfm)
		cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;

	if (reqcfm) {
		cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ACK; // we don't need ack any more
		ret = rwnx_hw->cmd_mgr->queue(rwnx_hw->cmd_mgr, cmd);
	} else {
#ifdef AICWF_SDIO_SUPPORT
		aicwf_set_cmd_tx((void *)(rwnx_hw->sdiodev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
#else
		aicwf_set_cmd_tx((void *)(rwnx_hw->usbdev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
#endif
	}

	if (!reqcfm || ret)
		rwnx_cmd_free(cmd);//kfree(cmd);

	return 0;
}

int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, u32 mem_data)
{
	struct dbg_mem_write_req *mem_write_req;

	/* Build the DBG_MEM_WRITE_REQ message */
	mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
									sizeof(struct dbg_mem_write_req));
	if (!mem_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_WRITE_REQ message */
	mem_write_req->memaddr = mem_addr;
	mem_write_req->memdata = mem_data;

	/* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL);
}

int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
							   struct dbg_mem_read_cfm *cfm)
{
	struct dbg_mem_read_req *mem_read_req;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	/* Build the DBG_MEM_READ_REQ message */
	mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID,
								   sizeof(struct dbg_mem_read_req));
	if (!mem_read_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_READ_REQ message */
	mem_read_req->memaddr = mem_addr;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_read_req, 1, DBG_MEM_READ_CFM, cfm);
}

int rwnx_send_dbg_mem_mask_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
									 u32 mem_mask, u32 mem_data)
{
	struct dbg_mem_mask_write_req *mem_mask_write_req;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	/* Build the DBG_MEM_MASK_WRITE_REQ message */
	mem_mask_write_req = rwnx_msg_zalloc(DBG_MEM_MASK_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
										 sizeof(struct dbg_mem_mask_write_req));
	if (!mem_mask_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_MASK_WRITE_REQ message */
	mem_mask_write_req->memaddr = mem_addr;
	mem_mask_write_req->memmask = mem_mask;
	mem_mask_write_req->memdata = mem_data;

	/* Send the DBG_MEM_MASK_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_mask_write_req, 1, DBG_MEM_MASK_WRITE_CFM, NULL);
}

int rwnx_send_rftest_req(struct rwnx_hw *rwnx_hw, u32_l cmd, u32_l argc, u8_l *argv, struct dbg_rftest_cmd_cfm *cfm)
{
	struct dbg_rftest_cmd_req *mem_rftest_cmd_req;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	/* Build the DBG_RFTEST_CMD_REQ message */
	mem_rftest_cmd_req = rwnx_msg_zalloc(DBG_RFTEST_CMD_REQ, TASK_DBG, DRV_TASK_ID,
										 sizeof(struct dbg_rftest_cmd_req));
	if (!mem_rftest_cmd_req)
		return -ENOMEM;

	if (argc > 10)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_MASK_WRITE_REQ message */
	mem_rftest_cmd_req->cmd = cmd;
	mem_rftest_cmd_req->argc = argc;
	if (argc != 0)
		memcpy(mem_rftest_cmd_req->argv, argv, argc);

	/* Send the DBG_RFTEST_CMD_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_rftest_cmd_req, 1, DBG_RFTEST_CMD_CFM, cfm);
}

int rwnx_send_txpwr_lvl_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_lvl_req *txpwr_lvl_req;
    txpwr_lvl_conf_v2_t txpwr_lvl_v2_tmp;
    txpwr_lvl_conf_v2_t *txpwr_lvl_v2;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_LVL_REQ message */
    txpwr_lvl_req = rwnx_msg_zalloc(MM_SET_TXPWR_IDX_LVL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_lvl_req));

    if (!txpwr_lvl_req) {
        return -ENOMEM;
    }

    txpwr_lvl_v2 = &txpwr_lvl_v2_tmp;

    get_userconfig_txpwr_lvl_v2_in_fdrv(txpwr_lvl_v2);

    if (txpwr_lvl_v2->enable == 0) {
        rwnx_msg_free(rwnx_hw, txpwr_lvl_req);
        return 0;
    } else {
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

        #if 0 // vnet driver: must in rftest mode
        if ((testmode == 0) && (chip_sub_id == 0)) {
            txpwr_lvl_req->txpwr_lvl.enable         = txpwr_lvl_v2->enable;
            txpwr_lvl_req->txpwr_lvl.dsss           = txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]; // 11M
            txpwr_lvl_req->txpwr_lvl.ofdmlowrate_2g4= txpwr_lvl_v2->pwrlvl_11ax_2g4[4]; // MCS4
            txpwr_lvl_req->txpwr_lvl.ofdm64qam_2g4  = txpwr_lvl_v2->pwrlvl_11ax_2g4[7]; // MCS7
            txpwr_lvl_req->txpwr_lvl.ofdm256qam_2g4 = txpwr_lvl_v2->pwrlvl_11ax_2g4[9]; // MCS9
            txpwr_lvl_req->txpwr_lvl.ofdm1024qam_2g4= txpwr_lvl_v2->pwrlvl_11ax_2g4[11]; // MCS11
            txpwr_lvl_req->txpwr_lvl.ofdmlowrate_5g = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm64qam_5g   = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm256qam_5g  = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm1024qam_5g = 13; // unused
        } else
        #endif
        {
            txpwr_lvl_req->txpwr_lvl_v2  = *txpwr_lvl_v2;
        }

        /* Send the MM_SET_TXPWR_LVL_REQ message to UMAC FW */
        error = rwnx_send_msg(rwnx_hw, txpwr_lvl_req, 1, MM_SET_TXPWR_IDX_LVL_CFM, NULL);

        return (error);
    }
}

int rwnx_send_txpwr_lvl_v3_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_lvl_req *txpwr_lvl_req;
    txpwr_lvl_conf_v3_t txpwr_lvl_v3_tmp;
    txpwr_lvl_conf_v3_t *txpwr_lvl_v3;
	txpwr_loss_conf_t txpwr_loss_tmp;
    txpwr_loss_conf_t *txpwr_loss;
    int error;
	int i;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_LVL_REQ message */
    txpwr_lvl_req = rwnx_msg_zalloc(MM_SET_TXPWR_IDX_LVL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_lvl_req));

    if (!txpwr_lvl_req) {
        return -ENOMEM;
    }

	txpwr_lvl_v3 = &txpwr_lvl_v3_tmp;
	txpwr_loss = &txpwr_loss_tmp;
	txpwr_loss->loss_enable_2g4 = 0;
	txpwr_loss->loss_enable_5g = 0;

    get_userconfig_txpwr_lvl_v3_in_fdrv(txpwr_lvl_v3);
    get_userconfig_txpwr_loss(txpwr_loss);

	if (txpwr_loss->loss_enable_2g4 == 1) {
		AICWFDBG(LOGINFO, "%s:loss_value_2g4: %d\r\n", __func__,
				 txpwr_loss->loss_value_2g4);

		for (i = 0; i <= 11; i++)
			txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[i] -= txpwr_loss->loss_value_2g4;
		for (i = 0; i <= 9; i++)
			txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[i] -= txpwr_loss->loss_value_2g4;
		for (i = 0; i <= 11; i++)
			txpwr_lvl_v3->pwrlvl_11ax_2g4[i] -= txpwr_loss->loss_value_2g4;
	}

	if (txpwr_loss->loss_enable_5g == 1) {
		AICWFDBG(LOGINFO, "%s:loss_value_5g: %d\r\n", __func__,
				 txpwr_loss->loss_value_5g);

		for (i = 0; i <= 11; i++)
			txpwr_lvl_v3->pwrlvl_11a_5g[i] -= txpwr_loss->loss_value_5g;
		for (i = 0; i <= 9; i++)
			txpwr_lvl_v3->pwrlvl_11n_11ac_5g[i] -= txpwr_loss->loss_value_5g;
		for (i = 0; i <= 11; i++)
			txpwr_lvl_v3->pwrlvl_11ax_5g[i] -= txpwr_loss->loss_value_5g;
	}

    if (txpwr_lvl_v3->enable == 0) {
        rwnx_msg_free(rwnx_hw, txpwr_lvl_req);
        return 0;
    } else {
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

        txpwr_lvl_req->txpwr_lvl_v3  = *txpwr_lvl_v3;

        /* Send the MM_SET_TXPWR_LVL_REQ message to UMAC FW */
        error = rwnx_send_msg(rwnx_hw, txpwr_lvl_req, 1, MM_SET_TXPWR_IDX_LVL_CFM, NULL);

        return (error);
    }
}

int rwnx_send_txpwr_lvl_v4_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_lvl_req *txpwr_lvl_req;
    txpwr_lvl_conf_v4_t txpwr_lvl_v4_tmp;
    txpwr_lvl_conf_v4_t *txpwr_lvl_v4;
	txpwr_loss_conf_t txpwr_loss_tmp;
    txpwr_loss_conf_t *txpwr_loss;
    int error;
	int i;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_LVL_REQ message */
    txpwr_lvl_req = rwnx_msg_zalloc(MM_SET_TXPWR_IDX_LVL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_lvl_req));

    if (!txpwr_lvl_req) {
        return -ENOMEM;
    }

    txpwr_lvl_v4 = &txpwr_lvl_v4_tmp;
    txpwr_loss = &txpwr_loss_tmp;
	txpwr_loss->loss_enable_2g4 = 0;
	txpwr_loss->loss_enable_5g = 0;

    get_userconfig_txpwr_lvl_v4_in_fdrv(txpwr_lvl_v4);
    get_userconfig_txpwr_loss(txpwr_loss);

	if (txpwr_loss->loss_enable_2g4 == 1) {
		AICWFDBG(LOGINFO, "%s:loss_value_2g4: %d\r\n", __func__,
				 txpwr_loss->loss_value_2g4);

		for (i = 0; i <= 11; i++)
			txpwr_lvl_v4->pwrlvl_11b_11ag_2g4[i] -= txpwr_loss->loss_value_2g4;
		for (i = 0; i <= 9; i++)
			txpwr_lvl_v4->pwrlvl_11n_11ac_2g4[i] -= txpwr_loss->loss_value_2g4;
		for (i = 0; i <= 11; i++)
			txpwr_lvl_v4->pwrlvl_11ax_2g4[i] -= txpwr_loss->loss_value_2g4;
	}

	if (txpwr_loss->loss_enable_5g == 1) {
		AICWFDBG(LOGINFO, "%s:loss_value_5g: %d\r\n", __func__,
				 txpwr_loss->loss_value_5g);

		for (i = 0; i <= 7; i++)
			txpwr_lvl_v4->pwrlvl_11a_5g[i] -= txpwr_loss->loss_value_5g;
		for (i = 0; i <= 9; i++)
			txpwr_lvl_v4->pwrlvl_11n_11ac_5g[i] -= txpwr_loss->loss_value_5g;
		for (i = 0; i <= 11; i++)
			txpwr_lvl_v4->pwrlvl_11ax_5g[i] -= txpwr_loss->loss_value_5g;
	}


    if (txpwr_lvl_v4->enable == 0) {
        rwnx_msg_free(rwnx_hw, txpwr_lvl_req);
        return 0;
    } else {
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

        txpwr_lvl_req->txpwr_lvl_v4  = *txpwr_lvl_v4;

        /* Send the MM_SET_TXPWR_LVL_REQ message to UMAC FW */
        error = rwnx_send_msg(rwnx_hw, txpwr_lvl_req, 1, MM_SET_TXPWR_IDX_LVL_CFM, NULL);

        return (error);
    }
}

