/**
 ****************************************************************************************
 *
 * @file fhost_rx.h
 *
 * @brief Definitions of the fully hosted RX task.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#ifndef _FHOST_RX_H_
#define _FHOST_RX_H_

/**
 ****************************************************************************************
 * @defgroup FHOST_RX FHOST_RX
 * @ingroup FHOST
 * @brief Fully Hosted RX task implementation.
 * This module creates a task that will be used to handle the RX descriptors passed by
 * the WiFi task.
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "net_def.h"
#include "rtos.h"
#include "cfgrwnx.h"
#include "co_math.h"
#include "co_list.h"
#include "rwnx_utils.h"
#include "rwnx_rx.h"
#if defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF)
#include "hostif_cfg.h"
#include "hostif.h"
#endif

#if (defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF) || (PLF_AIC8800 && defined(CFG_WIFI_HIB)))
#define FHOST_RX_REORDER 0
#else /* CONFIG_RWNX_LWIP && CFG_HOSTIF */
#define FHOST_RX_REORDER 1
#endif /* CONFIG_RWNX_LWIP && CFG_HOSTIF */

#if defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF)
#define FHOST_OR_HOSTIF 0
#else /* CONFIG_RWNX_LWIP && CFG_HOSTIF */
#define FHOST_OR_HOSTIF 1
#endif /* CONFIG_RWNX_LWIP && CFG_HOSTIF */

/* Radiotap °æ±¾ */
#define IEEE80211_RADIOTAP_VERSION 0

/* Present ×Ö¶Î±êÖ¾Î» */
#define IEEE80211_RADIOTAP_TSFT            0
#define IEEE80211_RADIOTAP_FLAGS           1
#define IEEE80211_RADIOTAP_RATE            2
#define IEEE80211_RADIOTAP_CHANNEL         3
#define IEEE80211_RADIOTAP_FHSS            4
#define IEEE80211_RADIOTAP_DBM_ANTSIGNAL   5
#define IEEE80211_RADIOTAP_DBM_ANTNOISE    6
#define IEEE80211_RADIOTAP_LOCK_QUALITY    7
#define IEEE80211_RADIOTAP_TX_ATTENUATION  8
#define IEEE80211_RADIOTAP_DB_TX_ATTENUATION 9
#define IEEE80211_RADIOTAP_DBM_TX_POWER    10
#define IEEE80211_RADIOTAP_ANTENNA         11
#define IEEE80211_RADIOTAP_DB_ANTSIGNAL    12
#define IEEE80211_RADIOTAP_DB_ANTNOISE     13
#define IEEE80211_RADIOTAP_RX_FLAGS        14
#define IEEE80211_RADIOTAP_TX_FLAGS        15
#define IEEE80211_RADIOTAP_RTS_RETRIES     16
#define IEEE80211_RADIOTAP_DATA_RETRIES    17
#define IEEE80211_RADIOTAP_MCS             18
#define IEEE80211_RADIOTAP_AMPDU_STATUS    19
#define IEEE80211_RADIOTAP_VHT             20
#define IEEE80211_RADIOTAP_TIMESTAMP       21
#define IEEE80211_RADIOTAP_HE              22
#define IEEE80211_RADIOTAP_HE_MU           23
#define IEEE80211_RADIOTAP_ZERO_LEN_PSDU   24
#define IEEE80211_RADIOTAP_LSIG            25

#define IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE 29

/* À©Õ¹»úÖÆ */
#define IEEE80211_RADIOTAP_EXT             31

/* ±êÖ¾Î»×Ó¶¨Òå */
#define IEEE80211_RADIOTAP_F_CFP           0x01
#define IEEE80211_RADIOTAP_F_SHORTPRE      0x02
#define IEEE80211_RADIOTAP_F_WEP           0x04
#define IEEE80211_RADIOTAP_F_FRAG          0x08
#define IEEE80211_RADIOTAP_F_FCS           0x10
#define IEEE80211_RADIOTAP_F_DATAPAD       0x20
#define IEEE80211_RADIOTAP_F_BADFCS        0x40

/* ³§ÉÌÃüÃû¿Õ¼ä */
#define IEEE80211_RADIOTAP_VENDOR_NAMESPACE 0x03

/* Values for formatModTx */
#define FORMATMOD_NON_HT          0
#define FORMATMOD_NON_HT_DUP_OFDM 1
#define FORMATMOD_HT_MF           2
#define FORMATMOD_HT_GF           3
#define FORMATMOD_VHT             4
#define FORMATMOD_HE_SU           5
#define FORMATMOD_HE_MU           6
#define FORMATMOD_HE_ER           7
/* ¶ÔÆëºê */
#define IEEE80211_RADIOTAP_ALIGN(len) (((len) + 3) & ~3)

/// Maximum number MSDUs supported in one received A-MSDU
#define NX_MAX_MSDU_PER_RX_AMSDU 8

/// Decryption status mask.
#define RX_HD_DECRSTATUS 0x0000007C

/// Decryption type offset
#define RX_HD_DECRTYPE_OFT 2
/// Frame decrypted using WEP.
#define RX_HD_DECR_WEP (0x01 << RX_HD_DECRTYPE_OFT)
/// Frame decrypted using TKIP.
#define RX_HD_DECR_TKIP (0x02 << RX_HD_DECRTYPE_OFT)
/// Frame decrypted using CCMP 128bits.
#define RX_HD_DECR_CCMP128 (0x03 << RX_HD_DECRTYPE_OFT)
/// Frame decrypted using WAPI.
#define RX_HD_DECR_WAPI (0x07 << RX_HD_DECRTYPE_OFT)

/// Packet contains an A-MSDU
#define RX_FLAGS_IS_AMSDU_BIT         CO_BIT(0)
/// Packet contains a 802.11 MPDU
#define RX_FLAGS_IS_MPDU_BIT          CO_BIT(1)
/// Packet contains 4 addresses
#define RX_FLAGS_4_ADDR_BIT           CO_BIT(2)
/// Packet is a Mesh Beacon received from an unknown Mesh STA
#define RX_FLAGS_NEW_MESH_PEER_BIT    CO_BIT(3)
#define RX_FLAGS_NEED_TO_REORD_BIT    CO_BIT(5)
#define RX_FLAGS_MONITOR_BIT          CO_BIT(7) //add by aic
#define RX_FLAGS_CSI_BIT              CO_BIT(9) //add by aic

/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

/// FHOST RX environment structure
struct fhost_rx_buf_tag
{
    union {
        net_buf_rx_t net_buf;
        struct co_list_hdr hdr;
        #if (AICWF_RX_REORDER)
        uint8_t reord_magic;
        #endif
    } net_hdr;

    /// Structure containing the information about the received payload
    struct rx_info info;
    /// Payload buffer space
    uint32_t payload[CO_ALIGN4_HI(FHOST_RX_BUF_SIZE)/sizeof(uint32_t)];
};
#define IPC_DATA_HDR_LEN    offsetof(struct fhost_rx_buf_tag, info)

#if NX_UF_EN
/// Structure for receive Vector 1
struct rx_vector_1_uf
{
    /// Contains the bytes 4 - 1 of Receive Vector 1
    uint32_t            recvec1a;
    /// Contains the bytes 8 - 5 of Receive Vector 1
    uint32_t            recvec1b;
    /// Contains the bytes 12 - 9 of Receive Vector 1
    uint32_t            recvec1c;
    /// Contains the bytes 16 - 13 of Receive Vector 1
    uint32_t            recvec1d;
};
struct rx_vector_desc
{
    /// Structure containing the information about the PHY channel that was used for this RX
    struct phy_channel_info phy_info;

    /// Structure containing the rx vector 1
    struct rx_vector_1 rx_vec_1;

    /// Used to mark a valid rx vector
    uint32_t pattern;
};
/// FHOST UF environment structure
struct fhost_rx_uf_buf_tag
{
    struct co_list_hdr hdr;
    struct rx_vector_desc rx_vector;

    /// Payload buffer space (empty in case of UF buffer)
    uint32_t payload[0];
};

/// Structure used, when receiving UF, for the buffer elements exchanged between the FHOST RX and the MAC thread
struct fhost_rx_uf_buf_desc_tag
{
    /// Id of the RX buffer
    uint32_t host_id;
};
#endif // NX_UF_EN

/// Structure used for the inter-task communication
struct fhost_rx_msg_tag
{
    /// RX descriptor structure
    //struct rxu_stat_val desc;
};

#define IS_QOS_DATA(frame_cntrl) ((frame_cntrl & MAC_FCTRL_TYPESUBTYPE_MASK) == MAC_FCTRL_QOS_DATA)

#if FHOST_RX_REORDER || ((defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF)) && !(AICWF_RX_REORDER))
#define RX_REORDER_START_FLAG (0x01)
#define NEED_REPLENISH_RX_BUF (0x10)

#define MAX_SEQNO_BY_TWO            2048
#define SEQNO_MASK                  0x0FFF
#define SEQNO_ADD(seq1, seq2) (((seq1) + (seq2)) & SEQNO_MASK)

/* This function checks whether seq1 is less than or equal to seq2 */
INLINE bool seqno_leq(uint16_t seq1, uint16_t seq2)
{
    if(((seq1 <= seq2) && ((seq2 - seq1) < MAX_SEQNO_BY_TWO)) ||
       ((seq1 > seq2) && ((seq1 - seq2) > MAX_SEQNO_BY_TWO)))
    {
        return true;
    }

    return false;
}

/* This function checks whether seq1 is less than seq2 */
INLINE bool seqno_lt(uint16_t seq1, uint16_t seq2)
{
    if(((seq1 < seq2) && ((seq2 - seq1) < MAX_SEQNO_BY_TWO)) ||
       ((seq1 > seq2) && ((seq1 - seq2) > MAX_SEQNO_BY_TWO)))
    {
        return true;
    }

    return false;
}

/* This function checks whether seq1 is greater than or equal to seq2 */
INLINE bool seqno_geq(uint16_t seq1, uint16_t seq2)
{
    return seqno_leq(seq2, seq1);
}

/* This function checks whether seq1 is greater than seq2 */
INLINE bool seqno_gt(uint16_t seq1, uint16_t seq2)
{
    return seqno_lt(seq2, seq1);
}
#endif /* FHOST_RX_REORDER */

/// FHOST RX environment structure
struct fhost_rx_env_tag
{
    #if FHOST_RX_REORDER || (defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF))
    struct co_list rx_reorder_list;
    rtos_semaphore rx_reorder_lock;
    TimerHandle_t  rx_reorder_timer;
    #endif /* FHOST_RX_REORDER */
    uint32_t  flags;
    uint16_t  next_rx_seq_no;
    #if 1//def CFG_HOSTAPD
    /// Management frame Callback function pointer
    cb_fhost_rx mgmt_cb[NX_VIRT_DEV_MAX];
    /// Management Callback parameter
    void *mgmt_cb_arg[NX_VIRT_DEV_MAX];
    #else
    /// Management frame Callback function pointer
    cb_fhost_rx mgmt_cb;
    /// Management Callback parameter
    void *mgmt_cb_arg;
    #endif
    /// Monitor Callback function pointer
    cb_fhost_rx monitor_cb;
    /// Monitor Callback parameter
    void *monitor_cb_arg;
};

/// Structure used for the buffer elements exchanged between the FHOST RX and the MAC thread
struct fhost_rx_buf_desc_tag
{
    /// Id of the RX buffer
    uint32_t host_id;
    /// Address of the payload inside the buffer
    uint32_t addr;
};
struct hw_rx_leg_vect
{
    uint8_t    dyn_bw_in_non_ht     : 1;
    uint8_t    chn_bw_in_non_ht     : 2;
    uint8_t    rsvd_nht             : 4;
    uint8_t    lsig_valid           : 1;
} __PACKED;

struct hw_rx_ht_vect
{
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
} __PACKED;

struct hw_rx_vht_vect
{
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
} __PACKED;
struct hw_rx_he_vect
{
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

    uint8_t  rsvd_he5               : 8;

    uint32_t  mcs                   : 4;
    uint32_t  nss                   : 3;
    uint32_t  fec                   : 1;
    uint32_t  length                :20;
    uint32_t  rsvd_he6              : 4;
}__PACKED;

struct hw_rx_vector_1 {
    uint8_t     format_mod         : 4;
    uint8_t     ch_bw              : 3;
    uint8_t     pre_type           : 1;
    uint8_t     antenna_set        : 8;
    int32_t     rssi_leg           : 8;
    uint32_t    leg_length         :12;
    uint32_t    leg_rate           : 4;
    int32_t     rssi1              : 8;

    union
    {
        struct hw_rx_leg_vect leg;
        struct hw_rx_ht_vect ht;
        struct hw_rx_vht_vect vht;
        struct hw_rx_he_vect he;
    };
} __PACKED;

struct hw_rx_vector_2 {
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
} __PACKED;

struct hw_rxhdr {
    /** Total length for the MPDU transfer */
    uint32_t len                   :16;

    uint32_t reserved              : 8;

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
#if 0
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
#endif
}__PACKED;

#if NX_UF_EN
struct uf_rx_vector_1 {
    uint8_t     format_mod         : 4;
    uint8_t     ch_bw              : 3;
    uint8_t     pre_type           : 1;
    uint8_t     antenna_set        : 8;
    int32_t     rssi_leg           : 8;
    uint32_t    leg_length         :12;
    uint32_t    leg_rate           : 4;
    int32_t     rssi1              : 8;

    union
    {
        struct rx_leg_vect leg;
        struct rx_ht_vect ht;
        struct rx_vht_vect vht;
        struct rx_he_vect he;
    };
} __PACKED;

__INLINE uint32_t hal_desc_get_vht_length(struct rx_vector_1_uf *rx_vec_1)
{
    uint32_t length;

    length = ((((rx_vec_1->recvec1d) & 0xF) << 16) | (((rx_vec_1->recvec1c) & 0xFFFF0000) >> 16));

    return length;
}
#endif /* NX_UF_EN */

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
/// FHOST RX environment
extern struct fhost_rx_env_tag fhost_rx_env;

/*
 * FUNCTIONS
 ****************************************************************************************
 */
#if 0
/**
 ****************************************************************************************
 * @brief Initialization of the RX task.
 * This function initializes the different data structures used for the RX and creates the
 * RTOS task dedicated to the RX processing.
 *
 * @return 0 on success and != 0 if error occurred.
 ****************************************************************************************
 */
int fhost_rx_init(void);
#endif

/**
 ****************************************************************************************
 * @brief Set the callback to call when receiving management frames (i.e. they have
 * not been processed by the wifi task).
 *
 * @attention The callback is called with a @ref fhost_frame_info parameter that is only
 * valid during the callback. If needed the callback is responsible to save the frame for
 * futher processing.
 *
 * @param[in] cb   Callback function pointer
 * @param[in] arg  Callback parameter (NULL if not needed)
 ****************************************************************************************
 */
void fhost_rx_set_mgmt_cb(uint8_t mac_vif_idx, cb_fhost_rx cb, void *arg);

/**
 ****************************************************************************************
 * @brief Set the callback to call when receiving packets in monitor mode.
 *
 * @attention The callback is called with a @ref fhost_frame_info parameter that is only
 * valid during the callback. If needed the callback is responsible to save the frame for
 * futher processing.
 *
 * @param[in] cb   Callback function pointer
 * @param[in] arg  Callback parameter (NULL if not needed)
 ****************************************************************************************
 */
void fhost_rx_set_monitor_cb(cb_fhost_rx cb, void *arg);
uint8_t machdr_len_get(uint16_t frame_cntl);
void fhost_rx_buf_push(void *net_buf);
void fhost_rx_buf_forward(struct fhost_rx_buf_tag *buf);
#if (defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF) && defined(CFG_SOFTAP))
int fhost_rxbuf_send_type(struct fhost_rx_buf_tag *buf, struct mac_addr *da, uint8_t *machdr_len);
#endif
void fhost_rx_monitor_cb(void *buf, bool uf);
int e2a_data_send(uint8_t *mac80211_data, uint32_t length);
#if defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF)
#ifdef CFG_HOSTIF_OPT
int e2a_data_send_direct(uint8_t *mac80211_data, uint32_t length);
void fhost_rx_buf_forward_copy_mcu(struct fhost_rx_buf_tag *buf);
#endif
#endif /* CONFIG_RWNX_LWIP & CFG_HOSTIF */

#if NX_CSI
void fhost_rx_set_csi_cb(cb_fhost_rx cb, void *arg);
void fhost_rx_csi_cb(void *buf);
#endif
#if NX_UF_EN
void fhost_rx_uf_buf_push(struct fhost_rx_uf_buf_tag *);
#endif /* NX_UF_EN */


struct ieee80211_radiotap_header {
    uint8_t it_version;
    uint8_t it_pad;
    uint16_t it_len;
    uint32_t it_present;
};
//#define IEEE80211_RADIOTAP_HE 23
//#define IEEE80211_RADIOTAP_HE_MU 24

struct ieee80211_radiotap_he {
	uint16_t data1, data2, data3, data4, data5, data6;
};

enum ieee80211_radiotap_he_bits {
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MASK		= 3,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU		= 0,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU	= 1,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU		= 2,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG		= 3,

	IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN	= 0x0004,
	IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN	= 0x0008,
	IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN	= 0x0020,
	IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN	= 0x0040,
	IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN	= 0x0100,
	IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN		= 0x0200,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN	= 0x0400,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN	= 0x0800,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN	= 0x1000,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN	= 0x2000,
	IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN	= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN	= 0x8000,

	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_KNOWN	= 0x0001,
	IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN		= 0x0002,
	IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN	= 0x0004,
	IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN	= 0x0008,
	IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN	= 0x0020,
	IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN		= 0x0040,
	IEEE80211_RADIOTAP_HE_DATA2_MIDAMBLE_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET		= 0x3f00,
	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN	= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_SEC	= 0x8000,

	IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR		= 0x003f,
	IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE		= 0x0040,
	IEEE80211_RADIOTAP_HE_DATA3_UL_DL		= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA3_DATA_MCS		= 0x0f00,
	IEEE80211_RADIOTAP_HE_DATA3_DATA_DCM		= 0x1000,
	IEEE80211_RADIOTAP_HE_DATA3_CODING		= 0x2000,
	IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG	= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA3_STBC		= 0x8000,

	IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE	= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_MU_STA_ID		= 0x7ff0,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1	= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2	= 0x00f0,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3	= 0x0f00,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4	= 0xf000,

	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC	= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ	= 0,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ	= 1,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ	= 2,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ	= 3,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_26T	= 4,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_52T	= 5,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_106T	= 6,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_242T	= 7,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_484T	= 8,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_996T	= 9,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_2x996T	= 10,

	IEEE80211_RADIOTAP_HE_DATA5_GI			= 0x0030,
	IEEE80211_RADIOTAP_HE_DATA5_GI_0_8			= 0,
	IEEE80211_RADIOTAP_HE_DATA5_GI_1_6			= 1,
	IEEE80211_RADIOTAP_HE_DATA5_GI_3_2			= 2,

	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE		= 0x00c0,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN		= 0,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X			= 1,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X			= 2,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X			= 3,
	IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS	= 0x0700,
	IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD		= 0x3000,
	IEEE80211_RADIOTAP_HE_DATA5_TXBF		= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG		= 0x8000,

	IEEE80211_RADIOTAP_HE_DATA6_NSTS		= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA6_DOPPLER		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA6_TXOP		= 0x7f00,
	IEEE80211_RADIOTAP_HE_DATA6_MIDAMBLE_PDCTY	= 0x8000,
};

struct ieee80211_radiotap_he_mu {
	uint16_t flags1, flags2;
	uint8_t ru_ch1[4];
	uint8_t ru_ch2[4];
};

enum ieee80211_radiotap_he_mu_bits {
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS		= 0x000f,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM		= 0x0020,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN		= 0x0040,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_CTR_26T_RU_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN		= 0x0100,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN		= 0x0200,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU_KNOWN	= 0x1000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU		= 0x2000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN	= 0x4000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN	= 0x8000,

	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW	= 0x0003,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_20MHZ	= 0x0000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_40MHZ	= 0x0001,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_80MHZ	= 0x0002,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_160MHZ	= 0x0003,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN	= 0x0004,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP		= 0x0008,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS	= 0x00f0,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW	= 0x0300,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN = 0x0400,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_CH2_CTR_26T_RU		= 0x0800,
};
/// @}

#endif // _FHOST_RX_H_
