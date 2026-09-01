/**
 * aicwf_def.h
 *
 * Copyright (C) AICSemi 2018-2025
 */

#ifndef _AICWF_DEF_H_
#define _AICWF_DEF_H_

// aicwifi ic
enum AICWF_IC
{
    PRODUCT_ID_AIC8800      = 0,
    PRODUCT_ID_AIC8800MC    = 1,
    PRODUCT_ID_AIC8800M40   = 2,
    PRODUCT_ID_AIC8800M80X2 = 3,
};

#ifdef CONFIG_RFTEST
extern int rftest_enable;
extern struct rwnx_hw *g_rwnx_hw;
#endif
extern struct rwnx_plat *g_rwnx_plat;



#endif /* _AICWF_DEF_H_ */

