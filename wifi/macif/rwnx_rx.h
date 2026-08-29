/**
 ******************************************************************************
 *
 * @file rwnx_rx.h
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#ifndef _RWNX_RX_H_
#define _RWNX_RX_H_

#include "fhost_config.h"       // CONFIG_RX_PKT_SNR

enum rx_status_bits
{
    /// The buffer can be forwarded to the networking stack
    RX_STAT_FORWARD = 1 << 0,
    /// A new buffer has to be allocated
    RX_STAT_ALLOC = 1 << 1,
    /// The buffer has to be deleted
    RX_STAT_DELETE = 1 << 2,
    /// The length of the buffer has to be updated
    RX_STAT_LEN_UPDATE = 1 << 3,
    /// The length in the Ethernet header has to be updated
    RX_STAT_ETH_LEN_UPDATE = 1 << 4,
    /// Simple copy
    RX_STAT_COPY = 1 << 5,
    /// Spurious frame (inform upper layer and discard)
    RX_STAT_SPURIOUS = 1 << 6,
    /// packet for monitor interface
    RX_STAT_MONITOR = 1 << 7,
};


/*
 * Decryption status subfields.
 * {
 */
#define RWNX_RX_HD_DECR_UNENC           0 // Frame unencrypted
#define RWNX_RX_HD_DECR_ICVFAIL         1 // WEP/TKIP ICV failure
#define RWNX_RX_HD_DECR_CCMPFAIL        2 // CCMP failure
#define RWNX_RX_HD_DECR_AMSDUDISCARD    3 // A-MSDU discarded at HW
#define RWNX_RX_HD_DECR_NULLKEY         4 // NULL key found
#define RWNX_RX_HD_DECR_WEPSUCCESS      5 // Security type WEP
#define RWNX_RX_HD_DECR_TKIPSUCCESS     6 // Security type TKIP
#define RWNX_RX_HD_DECR_CCMPSUCCESS     7 // Security type CCMP (or WPI)
// @}


/// Structure containing information about the received frame (length, timestamp, rate, etc.)
struct rx_vector
{
    /// Total length of the received MPDU
    uint16_t            frmlen;
    /// AMPDU status information
    uint16_t            ampdu_stat_info;
    /// TSF Low
    uint32_t            tsflo;
    /// TSF High
    uint32_t            tsfhi;
    /// Contains the bytes 4 - 1 of Receive Vector 1
    uint32_t            recvec1a;
    /// Contains the bytes 8 - 5 of Receive Vector 1
    uint32_t            recvec1b;
    /// Contains the bytes 12 - 9 of Receive Vector 1
    uint32_t            recvec1c;
    /// Contains the bytes 16 - 13 of Receive Vector 1
    uint32_t            recvec1d;
    /// Contains the bytes 4 - 1 of Receive Vector 2
    uint32_t            recvec2a;
    ///  Contains the bytes 8 - 5 of Receive Vector 2
    uint32_t            recvec2b;
    /// MPDU status information
    uint32_t            statinfo;
};

/// Structure containing the information about the PHY channel that was used for this RX
struct phy_channel_info
{
    /// PHY channel information 1
    uint32_t info1;
    /// PHY channel information 2
    uint32_t info2;
};

/// Structure containing the information about the received payload
struct rx_info
{
    /// Rx header descriptor (this element MUST be the first of the structure)
    struct rx_vector vect;
    /// Structure containing the information about the PHY channel that was used for this RX
    struct phy_channel_info phy_info;
    /// Word containing some SW flags about the RX packet
    uint32_t flags;
    #if NX_AMSDU_DEAGG
    /// Array of host buffer identifiers for the other A-MSDU subframes
    uint32_t amsdu_hostids[NX_MAX_MSDU_PER_RX_AMSDU - 1];
    #endif
    /// Spare room for LMAC FW to write a pattern when last DMA is sent
    uint32_t pattern;

    uint16_t reserved[2];
};

#if PLF_RSSI_DATAPKT
int8_t data_pkt_rssi_get(void);
#endif /* PLF_RSSI_DATAPKT */

#if PLF_AIC8800M40
#if CONFIG_RX_PKT_SNR
int8_t data_pkt_snr_get(void);
#endif
#endif

#if (PLF_AIC8800) || (PLF_AIC8800M40)
#define AICWF_RX_REORDER 1
#define AICWF_RWNX_TIMER_EN 1
#else
#define AICWF_RX_REORDER 0
#define AICWF_RWNX_TIMER_EN 0
#endif

#if (AICWF_RWNX_TIMER_EN)
typedef void (*rwnx_timer_cb_t)(void * arg);

enum rwnx_timer_state_e {
    RWNX_TIMER_STATE_FREE   = 0,
    RWNX_TIMER_STATE_POST   = 1,
    RWNX_TIMER_STATE_STOP   = 2,
};

enum rwnx_timer_action_e {
    RWNX_TIMER_ACTION_CREATE    = 0,
    RWNX_TIMER_ACTION_START     = 1,
    RWNX_TIMER_ACTION_RESTART   = 2,
    RWNX_TIMER_ACTION_STOP      = 3,
    RWNX_TIMER_ACTION_DELETE    = 4,
};

struct rwnx_timer_node_s {
    struct co_list_hdr hdr;
    rwnx_timer_cb_t cb;
    void *arg;
    uint32_t expired_ms; // remained
    enum rwnx_timer_state_e state;
    bool periodic;
    bool auto_load;
};

typedef struct rwnx_timer_node_s * rwnx_timer_handle;
#endif

#if (AICWF_RX_REORDER)
#define MAX_REORD_RXFRAME       250
#define REORDER_UPDATE_TIME             20
#define AICWF_REORDER_WINSIZE           8
#define AICWF_REORDER_MAX_HOLD          6
#define AICWF_REORDER_GLOBAL_MAX_HOLD   6
#define AICWF_REORDER_LOW_HEAP_BYTES    (12U * 1024U)
#define SN_LESS(a, b)           (((a-b)&0x800) != 0)
#define SN_EQUAL(a, b)          (a == b)

struct reord_ctrl {
    uint8_t enable;
    uint8_t wsize_b;
    uint16_t ind_sn;
    uint16_t list_cnt;
    struct co_list reord_list;
    rtos_mutex reord_list_lock;
    #if (AICWF_RWNX_TIMER_EN)
    rwnx_timer_handle reord_timer;
    #else
    TimerHandle_t reord_timer;
    #endif
};

struct reord_ctrl_info {
    struct co_list_hdr hdr;
    uint8_t mac_addr[6];
    struct reord_ctrl preorder_ctrl[8];
};

struct fhost_rx_buf_tag;

struct recv_msdu {
    struct co_list_hdr hdr;
    struct fhost_rx_buf_tag *rx_buf;
    uint16_t buf_len;
    uint16_t seq_num;
    uint8_t  tid;
    uint8_t  forward;
    //struct reord_ctrl *preorder_ctrl;
};
#endif

struct rx_vector_1_old {
	/** Receive Vector 1a */
	uint32_t    leg_length         :12;
	uint32_t    leg_rate           : 4;
	uint32_t    ht_length          :16;

	/** Receive Vector 1b */
	uint32_t    _ht_length         : 4; // FIXME
	uint32_t    short_gi           : 1;
	uint32_t    stbc               : 2;
	uint32_t    smoothing          : 1;
	uint32_t    mcs                : 7;
	uint32_t    pre_type           : 1;
	uint32_t    format_mod         : 3;
	uint32_t    ch_bw              : 2;
	uint32_t    n_sts              : 3;
	uint32_t    lsig_valid         : 1;
	uint32_t    sounding           : 1;
	uint32_t    num_extn_ss        : 2;
	uint32_t    aggregation        : 1;
	uint32_t    fec_coding         : 1;
	uint32_t    dyn_bw             : 1;
	uint32_t    doze_not_allowed   : 1;

	/** Receive Vector 1c */
	uint32_t    antenna_set        : 8;
	uint32_t    partial_aid        : 9;
	uint32_t    group_id           : 6;
	uint32_t    first_user         : 1;
	int32_t    rssi1              : 8;

	/** Receive Vector 1d */
	int32_t    rssi2              : 8;
	int32_t    rssi3              : 8;
	int32_t    rssi4              : 8;
	uint32_t    reserved_1d        : 8;
};
#if 1

struct rx_leg_vect {
	uint8_t    dyn_bw_in_non_ht     : 1;
	uint8_t    chn_bw_in_non_ht     : 2;
	uint8_t    rsvd_nht             : 4;
	uint8_t    lsig_valid           : 1;
} __packed;

struct rx_ht_vect {
	uint16_t   sounding             : 1;
	uint16_t   smoothing            : 1;
	uint16_t   short_gi             : 1;
	uint16_t   aggregation          : 1;
	uint16_t   stbc                 : 1;
	uint16_t   num_extn_ss          : 2;
	uint16_t   lsig_valid           : 1;
	uint16_t   mcs                  : 7;
	uint16_t   fec                  : 1;
	uint16_t   length               :16;
} __packed;

struct rx_vht_vect {
	uint8_t   sounding              : 1;
	uint8_t   beamformed            : 1;
	uint8_t   short_gi              : 1;
	uint8_t   rsvd_vht1             : 1;
	uint8_t   stbc                  : 1;
	uint8_t   doze_not_allowed      : 1;
	uint8_t   first_user            : 1;
	uint8_t   rsvd_vht2             : 1;
	uint16_t  partial_aid           : 9;
	uint16_t  group_id              : 6;
	uint16_t  rsvd_vht3             : 1;
	uint32_t  mcs                   : 4;
	uint32_t  nss                   : 3;
	uint32_t  fec                   : 1;
	uint32_t  length                :20;
	uint32_t  rsvd_vht4             : 4;
} __packed;

struct rx_he_vect {
	uint8_t   sounding              : 1;
	uint8_t   beamformed            : 1;
	uint8_t   gi_type               : 2;
	uint8_t   stbc                  : 1;
	uint8_t   rsvd_he1              : 3;

	uint8_t   uplink_flag           : 1;
	uint8_t   beam_change           : 1;
	uint8_t   dcm                   : 1;
	uint8_t   he_ltf_type           : 2;
	uint8_t   doppler               : 1;
	uint8_t   rsvd_he2              : 2;

	uint8_t   bss_color             : 6;
	uint8_t   rsvd_he3              : 2;

	uint8_t   txop_duration         : 7;
	uint8_t   rsvd_he4              : 1;

	uint8_t   pe_duration           : 4;
	uint8_t   spatial_reuse         : 4;

	uint8_t   sig_b_comp_mode       : 1;
	uint8_t   dcm_sig_b             : 1;
	uint8_t   mcs_sig_b             : 3;
	uint8_t   ru_size               : 3;

	uint32_t  mcs                   : 4;
	uint32_t  nss                   : 3;
	uint32_t  fec                   : 1;
	uint32_t  length                :20;
	uint32_t  rsvd_he6              : 4;
} __packed;
#endif

struct rx_vector_1 {
	uint8_t     format_mod         : 4;
	uint8_t     ch_bw              : 3;
	uint8_t     pre_type           : 1;
	uint8_t     antenna_set        : 8;
	int32_t    rssi_leg           : 8;
	uint32_t    leg_length         :12;
	uint32_t    leg_rate           : 4;
	int32_t    rssi1              : 8;

	union {
		struct rx_leg_vect leg;
		struct rx_ht_vect ht;
		struct rx_vht_vect vht;
		struct rx_he_vect he;
	};
} __packed;
struct rx_vector_2_old {
	/** Receive Vector 2a */
	uint32_t    rcpi               : 8;
	uint32_t    evm1               : 8;
	uint32_t    evm2               : 8;
	uint32_t    evm3               : 8;

	/** Receive Vector 2b */
	uint32_t    evm4               : 8;
	uint32_t    reserved2b_1       : 8;
	uint32_t    reserved2b_2       : 8;
	uint32_t    reserved2b_3       : 8;

};

struct rx_vector_2 {
	/** Receive Vector 2a */
	uint32_t    rcpi1              : 8;
	uint32_t    rcpi2              : 8;
	uint32_t    rcpi3              : 8;
	uint32_t    rcpi4              : 8;

	/** Receive Vector 2b */
	uint32_t    evm1               : 8;
	uint32_t    evm2               : 8;
	uint32_t    evm3               : 8;
	uint32_t    evm4               : 8;
};

struct phy_channel_info_desc {
	/** PHY channel information 1 */
	uint32_t    phy_band           : 8;
	uint32_t    phy_channel_type   : 8;
	uint32_t    phy_prim20_freq    : 16;
	/** PHY channel information 2 */
	uint32_t    phy_center1_freq   : 16;
	uint32_t    phy_center2_freq   : 16;
};

struct hw_vect {
	/** Total length for the MPDU transfer */
	uint32_t len                   :16;

	uint32_t reserved              : 8;//data type is included
	/** AMPDU Status Information */
	uint32_t mpdu_cnt              : 6;
	uint32_t ampdu_cnt             : 2;

	/** TSF Low */
	uint32_t tsf_lo;
	/** TSF High */
	uint32_t tsf_hi;

	/** Receive Vector 1 */
	struct rx_vector_1 rx_vect1;
	/** Receive Vector 2 */
	struct rx_vector_2 rx_vect2;

	/** Status **/
	uint32_t    rx_vect2_valid     : 1;
	uint32_t    resp_frame         : 1;
	/** Decryption Status */
	uint32_t    decr_status        : 3;
	uint32_t    rx_fifo_oflow      : 1;

	/** Frame Unsuccessful */
	uint32_t    undef_err          : 1;
	uint32_t    phy_err            : 1;
	uint32_t    fcs_err            : 1;
	uint32_t    addr_mismatch      : 1;
	uint32_t    ga_frame           : 1;
	uint32_t    current_ac         : 2;

	uint32_t    frm_successful_rx  : 1;
	/** Descriptor Done  */
	uint32_t    desc_done_rx       : 1;
	/** Key Storage RAM Index */
	uint32_t    key_sram_index     : 10;
	/** Key Storage RAM Index Valid */
	uint32_t    key_sram_v         : 1;
	uint32_t    type               : 2;
	uint32_t    subtype            : 4;
};
/**
 * struct rwnx_rx_rate_stats - Store statistics for RX rates
 *
 * @table: Table indicating how many frame has been receive which each
 * rate index. Rate index is the same as the one used by RC algo for TX
 * @size: Size of the table array
 * @cpt: number of frames received
 * @rate_cnt: number of rate for which at least one frame has been received
 */
struct rwnx_rx_rate_stats {
	int *table;
	int size;
	int cpt;
	int rate_cnt;
};

struct rwnx_legrate {
	int idx;
	int rate;
};

uint8_t rwnx_rxdataind(void *pthis, void *hostid);
void rwnx_reord_init(void);
void reord_deinit_sta_by_mac(const uint8_t *mac_addr);

#if NX_UF_EN
uint8_t rwnx_unsup_rx_vec_ind(void *pthis, void *hostid);
#endif

#endif /* _RWNX_RX_H_ */
