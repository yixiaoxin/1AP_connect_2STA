#ifndef __BT_AIC8800_DRV_CONFIG_H__
#define  __BT_AIC8800_DRV_CONFIG_H__
#include "compiler.h"
#include "reg_access.h"

#include "fw_adid_u02.h"
#include "fw_patch_u02.h"
#include "fw_patch_table_u02.h"
#include "fw_patch_u02_ext0.h"

#include "fw_adid_u02h.h"
#include "fw_patch_u02h.h"
#include "fw_patch_table_u02h.h"
#include "fw_patch_u02h_ext0.h"

#define AICBT_PT_INF          0x00
#define AICBT_PT_TRAP         0x01
#define AICBT_PT_B4           0x02
#define AICBT_PT_BTMODE       0x03
#define AICBT_PT_PWRON        0x04
#define AICBT_PT_AF           0x05
#define AICBT_PT_VER          0x06

#define AICBT_PT_TAG_SIZE                   16
#define AICBT_PT_TAG                        "AICBT_PT_TAG"
#define AICBT_PT_TRAP_TAG                   "AICBT_TRAP_T";
#define AICBT_PT_PATCH_TB4_TAG              "AICBT_PATCH_TB4";
#define AICBT_PT_MODE_TAG                   "AICBT_MODE_T";
#define AICBT_PT_PWRON_TAG                  "AICBT_POWER_ON";
#define AICBT_PT_PATCH_TAF_TAG              "AICBT_PATCH_TAF";

enum aic_fw{
    FW_ADID_8800DC,
    FW_PATCH_8800DC,
    FW_PATCH_8800DC_EXT,
    FW_PATCH_TABLE_8800DC,
};

struct aicbt_patch_table {
    char     *name;
    uint32_t type;
    uint32_t *data;
    uint32_t len;
    struct aicbt_patch_table *next;
};

struct aicbt_patch_info_t {
    uint32_t info_len;
//base len start
    uint32_t adid_addrinf;
    uint32_t addr_adid;
    uint32_t patch_addrinf;
    uint32_t addr_patch;
    uint32_t reset_addr;
    uint32_t reset_val;
    uint32_t adid_flag_addr;
    uint32_t adid_flag;
//base len end
//ext patch nb
    uint32_t ext_patch_nb_addr;
    uint32_t ext_patch_nb;
    uint32_t *ext_patch_param;
};
int aicfw_download_fw(void);
#endif
