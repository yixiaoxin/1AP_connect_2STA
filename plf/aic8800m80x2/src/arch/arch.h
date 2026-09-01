/**
 ****************************************************************************************
 *
 * @file arch.h
 *
 * @brief This file contains the definitions of the macros and functions that are
 * architecture dependent.  The implementation of those is implemented in the
 * appropriate architecture directory.
 *
 ****************************************************************************************
 */

#ifndef _ARCH_H_
#define _ARCH_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "plf.h"           // SW configuration

#include <stdint.h>        // standard integer definition
#include "compiler.h"      // inline functions

/*
 * CPU WORD SIZE
 ****************************************************************************************
 */
/// ARM is a 32-bit CPU
#define CPU_WORD_SIZE   4

/*
 * CPU Endianness
 ****************************************************************************************
 */
/// ARM is little endian
#define CPU_LE          1


/*
 * DEFINES
 ****************************************************************************************
 */
#ifndef NULL
#define NULL (void *)0
#endif

/** Macros to compute minimum and maximum */
#ifndef MAX
#define MAX(A, B)       (((A) > (B)) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A, B)       (((A) < (B)) ? (A) : (B))
#endif
/** Calculate array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * TYPES
 ****************************************************************************************
 */
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int32_t  s32;
typedef int16_t  s16;
typedef int8_t   s8;

#ifndef size_t
typedef unsigned int        size_t;
#endif /* size_t */

/*
 * EXPORTED FUNCTION DECLARATION
 ****************************************************************************************
 */


// required to define GLOBAL_INT_** macros as inline assembly. This file is included after
// definition of ASSERT macros as they are used inside ll.h
#include "ll.h"     // ll definitions

#endif // _ARCH_H_
