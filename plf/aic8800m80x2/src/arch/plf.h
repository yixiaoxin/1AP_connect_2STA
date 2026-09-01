/**
 ****************************************************************************************
 *
 * @file plf.h
 *
 * @brief This file contains the definitions of the macros and functions that are
 * platform dependent.  The implementation of those is implemented in the
 * appropriate platform directory.
 *
 ****************************************************************************************
 */
#ifndef _PLF_H_
#define _PLF_H_

/**
 ****************************************************************************************
 * @defgroup PLF
 * @ingroup DRIVERS
 *
 * @brief Platform register driver
 *
 * @{
 *
 ****************************************************************************************
 */

/*
 * Platform
 ****************************************************************************************
 */
#if defined(CFG_AIC8800)
#define PLF_AIC8800         1
#define PLF_AIC8800MC       0
#define PLF_AIC8800M40      0
#define PLF_AIC8800M80X2    0
#elif defined(CFG_AIC8800MC)
#define PLF_AIC8800         0
#define PLF_AIC8800MC       1
#define PLF_AIC8800M40      0
#define PLF_AIC8800M80X2    0
#elif defined(CFG_AIC8800M40)
#define PLF_AIC8800         0
#define PLF_AIC8800MC       0
#define PLF_AIC8800M40      1
#define PLF_AIC8800M80X2    0
#elif defined(CFG_AIC8800M80X2)
#define PLF_AIC8800         0
#define PLF_AIC8800MC       0
#define PLF_AIC8800M40      0
#define PLF_AIC8800M80X2    1
#else //CFG_AIC8800
#error "Invalid platform"
#endif //CFG_AIC8800

/*
 * HW Platform
 ****************************************************************************************
 */
#if defined(CFG_HW_PLATFORM) && (CFG_HW_PLATFORM <= 2)
#if   CFG_HW_PLATFORM == 0
#define PLF_HW_PXP          1
#define PLF_HW_FPGA         0
#define PLF_HW_ASIC         0
#elif CFG_HW_PLATFORM == 1
#define PLF_HW_PXP          0
#define PLF_HW_FPGA         1
#define PLF_HW_ASIC         0
#elif CFG_HW_PLATFORM == 2
#define PLF_HW_PXP          0
#define PLF_HW_FPGA         0
#define PLF_HW_ASIC         1
#endif
#else // CFG_HW_PLATFORM
#error "Invalid HW platform"
#endif // CFG_HW_PLATFORM

/*
 * DEBUG configuration
 ****************************************************************************************
 */
#if defined(CFG_DBG)
#define PLF_DEBUG           1
#else //CFG_DBG
#define PLF_DEBUG           0
#endif //CFG_DBG

/*
 * STDLIB
 ****************************************************************************************
 */
#ifdef CFG_STDLIB
#define PLF_STDLIB          1
#else // CFG_STDLIB
#define PLF_STDLIB          0
#endif // CFG_STDLIB

/*
 * CONSOLE
 ****************************************************************************************
 */
#ifdef CFG_CONSOLE
#define PLF_CONSOLE         1
#else // CFG_CONSOLE
#define PLF_CONSOLE         0
#endif // CFG_CONSOLE

/*
 * Letter Shell
 ****************************************************************************************
 */
#ifdef CFG_LETTER_SHELL
#define PLF_LETTER_SHELL    1
#else // CFG_LETTER_SHELL
#define PLF_LETTER_SHELL    0
#endif // CFG_LETTER_SHELL

/*
 * SOFTWDG
 ****************************************************************************************
 */
#ifdef CFG_MODULE_SOFTWDG
#define PLF_MODULE_SOFTWDG  1
#else // CFG_MODULE_SOFTWDG
#define PLF_MODULE_SOFTWDG  0
#endif // CFG_MODULE_SOFTWDG

/*
 * TEMP_COMP
 ****************************************************************************************
 */
#ifdef CFG_MODULE_TEMP_COMP
#define PLF_MODULE_TEMP_COMP    1
#else // CFG_MODULE_TEMP_COMP
#define PLF_MODULE_TEMP_COMP    0
#endif // CFG_MODULE_TEMP_COMP

/*
 * OTA
 ****************************************************************************************
 */
#ifdef CFG_OTA
#define PLF_OTA             1
#else // CFG_OTA
#define PLF_OTA             0
#endif // CFG_OTA

/*
 * Test Case
 ****************************************************************************************
 */
#ifdef CFG_TEST
#define PLF_TEST            1
#else // CFG_TEST
#define PLF_TEST            0
#endif // CFG_TEST

/*
 * Wi-Fi Stack
 ****************************************************************************************
 */
#ifdef CFG_WIFI_STACK
#define PLF_WIFI_STACK      1
#else // CFG_WIFI_STACK
#define PLF_WIFI_STACK      0
#endif // CFG_WIFI_STACK

/*
 * 5G Band
 ****************************************************************************************
 */
#ifdef CFG_BAND5G
#define PLF_BAND5G          1
#else // CFG_BAND5G
#define PLF_BAND5G          0
#endif // CFG_BAND5G

/*
 * BT Stack
 ****************************************************************************************
 */
#ifdef CFG_BT_STACK
#define PLF_BT_STACK        1
#else // CFG_BT_STACK
#define PLF_BT_STACK        0
#endif // CFG_BT_STACK

/*
 * BLE Stack
 ****************************************************************************************
 */
#ifdef CFG_BLE_STACK
#define PLF_BLE_STACK       1
#else // CFG_BLE_STACK
#define PLF_BLE_STACK       0
#endif // CFG_BLE_STACK

/*
 * BLE Stack only, not support br task
 ****************************************************************************************
 */
#ifdef CFG_BLE_ONLY
#define PLF_BLE_ONLY       1
#else // CFG_BLE_ONLY
#define PLF_BLE_ONLY       0
#endif // CFG_BLE_ONLY

/*
 * BTDM bt/ble combo
 ****************************************************************************************
 */
#ifdef CFG_BTDM
#define PLF_BTDM            1
#else // CFG_BTDM
#define PLF_BTDM            0
#endif // CFG_BTDM

/*
 * BT Customer
 ****************************************************************************************
 */
#ifdef CFG_BT_USER
#define PLF_BT_USER     1
#else // CFG_BT_USER
#define PLF_BT_USER     0
#endif // CFG_BT_USER

/*
 * BT Ota box
 ****************************************************************************************
 */
#ifdef CFG_BT_OTA
#define PLF_BT_OTA     1
#else // CFG_BT_OTA
#define PLF_BT_OTA     0
#endif // CFG_BT_OTA

/*
 * USB_BT
 ****************************************************************************************
 */
#ifdef CFG_USB_BT
#define PLF_USB_BT     1
#else // CFG_USB_BT
#define PLF_USB_BT     0
#endif // CFG_USB_BT

/*
 * BLE_M2D
 ****************************************************************************************
 */
#ifdef CFG_M2D_BLE
#define PLF_M2D_BLE     1
#else // CFG_M2D_BLE
#define PLF_M2D_BLE     0
#endif // CFG_M2D_BLE

/*
 * M2D_OTA
 ****************************************************************************************
 */
#ifdef CFG_M2D_OTA
#define PLF_M2D_OTA     1
#else // CFG_M2D_OTA
#define PLF_M2D_OTA     0
#endif // CFG_M2D_OTA

/*
 * LED support
 ****************************************************************************************
 */
#ifdef CFG_LED_MODULE
#define PLF_LED_MODULE     1
#else // CFG_LED_MODULE
#define PLF_LED_MODULE     0
#endif // CFG_LED_MODULE

/*
 * KEY support
 ****************************************************************************************
 */
#ifdef CFG_KEY_MODULE
#define PLF_KEY_MODULE     1
#else // CFG_LED_MODULE
#define PLF_KEY_MODULE     0
#endif // CFG_LED_MODULE

/*
 * LS support
 ****************************************************************************************
 */
#ifdef CFG_LIGHT_SENSOR
#define PLF_LS_MODULE      1
#else // CFG_LIGHT_SENSOR
#define PLF_LS_MODULE      0
#endif // CFG_LIGHT_SENSOR

/*
 * I2CM_SFT support
 ****************************************************************************************
 */
#ifdef CFG_I2CM_SFT
#define PLF_I2CM_SFT      1
#else // CFG_I2CM_SFT
#define PLF_I2CM_SFT      0
#endif // CFG_I2CM_SFT

/*
 * AON Support
 ****************************************************************************************
 */
#ifdef CFG_AON
#define PLF_AON_SUPPORT     1
#else // CFG_AON
#define PLF_AON_SUPPORT     0
#endif // CFG_AON

/*
 * HFP AG Support
 ****************************************************************************************
 */
#ifdef CFG_HFP_AG_SUPPORT
#define PLF_HFG_SUPPORT     1
#else // CFG_HFP_AG_SUPPORT
#define PLF_HFG_SUPPORT     0
#endif // CFG_HFP_AG_SUPPORT

/*
 * AIC_VENDOR_ADV Support
 ****************************************************************************************
 */
#ifdef CFG_AIC_VENDOR_ADV
#define PLF_AIC_VENDOR_ADV     1
#else // CFG_AIC_VENDOR_ADV
#define PLF_AIC_VENDOR_ADV     0
#endif // CFG_AIC_VENDOR_ADV
/*
 * Audio
 ****************************************************************************************
 */
#ifdef CFG_AUD_USED
#define PLF_AUD_USED        1
#else
#define PLF_AUD_USED        0
#endif

#ifdef CFG_ASIO
#define PLF_ASIO            1
#else // CFG_ASIO
#define PLF_ASIO            0
#endif // CFG_ASIO

#ifdef CFG_AUDIO
#define PLF_AUDIO           1
#else // CFG_AUDIO
#define PLF_AUDIO           0
#endif // CFG_AUDIO

#ifdef CFG_BT_AUDIO
#define PLF_BT_AUDIO        1
#else // CFG_BT_AUDIO
#define PLF_BT_AUDIO        0
#endif // CFG_BT_AUDIO

#ifdef CFG_BT_PROMPT
#define PLF_BT_PROMPT       1
#else // CFG_BT_PROMPT
#define PLF_BT_PROMPT       0
#endif // CFG_BT_PROMPT

#ifdef CFG_WIFI_AUDIO
#define PLF_WIFI_AUDIO        1
#else // CFG_WIFI_AUDIO
#define PLF_WIFI_AUDIO        0
#endif // CFG_WIFI_AUDIO

#if defined(CFG_AIC1000_MIC_MATRIX) && (CFG_AIC1000_MIC_MATRIX < 6)
#define PLF_AIC1000_MIC_MATRIX    CFG_AIC1000_MIC_MATRIX
#else // CFG_AIC1000_MIC_MATRIX
#error "Invalid AIC1000 MIC MATRIX"
#endif // CFG_AIC1000_MIC_MATRIX

#ifdef CFG_EXT_AIC1000
#define PLF_EXT_AIC1000        1
#else // CFG_EXT_AIC1000
#define PLF_EXT_AIC1000        0
#endif // CFG_EXT_AIC1000

/*
 * HCLK option: Generate I2S MCLK
 ****************************************************************************************
 */
#ifdef CFG_HCLK_MCLK
#define PLF_HCLK_MCLK   1
#else // CFG_HCLK_MCLK
#define PLF_HCLK_MCLK   0
#endif // CFG_HCLK_MCLK

#ifdef CFG_BLE_WAKEUP
#define PLF_BLE_WAKEUP    1
#else // CFG_BLE_WAKEUP
#define PLF_BLE_WAKEUP    0
#endif // CFG_BLE_WAKEUP

/*
 * BT test mode flash bin Support
 ****************************************************************************************
 */
#ifdef CFG_BT_TESTMODE
#define PLF_BT_TESTMODE     1
#else // CFG_BT_TESTMODE
#define PLF_BT_TESTMODE     0
#endif // CFG_BT_TESTMODE

/*
 * PMIC is valid or not
 ****************************************************************************************
 */
#ifdef CFG_PMIC
#define PLF_PMIC            1
#else // CFG_PMIC
#define PLF_PMIC            0
#endif // CFG_PMIC

/*
 * GSENSOR
 ****************************************************************************************
 */
#ifdef CFG_GSENSOR
#define PLF_GSENSOR         1
#else  // CFG_GSENSOR
#define PLF_GSENSOR         0
#endif // CFG_GSENSOR

/*
 * FATFS
 ****************************************************************************************
 */
#ifdef CFG_FATFS
#define PLF_FATFS           1
#else  // CFG_FATFS
#define PLF_FATFS           0
#endif // CFG_FATFS

/*
 * DSP
 ****************************************************************************************
 */
#ifdef CFG_DSP
#define PLF_DSP           1
#else  // CFG_DSP
#define PLF_DSP           0
#endif // CFG_DSP

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */


/// @} PLF

#endif // _PLF_H_
