/*
 * Copyright (C) 2018-2023 AICSemi Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _SPI_API_H_
#define _SPI_API_H_

/**
 * Includes
 */
#include <stdbool.h>
#include "chip.h"
#include "reg_spi.h"

/* ------------------ spi api version 1 ------------------ */
/**
 * Macros
 */

/**
 * TypeDefs
 */
typedef enum {
    SPI_CS0 = 0, // hw default csn pin
    SPI_CS1,     // user defined gpioa
    SPI_CSN_MAX
} SPI_CSN_T;

typedef enum {
    SPI_DC_NONE = 0,
    SPI_DC_DATA,
    SPI_DC_CMD,
    SPI_DC_MAX
} SPI_DC_T;

typedef enum {
    SPI_MODE_GENERIC      = 0,
    SPI_MODE_LCD_3W_DIO   = 1,
    SPI_MODE_LCD_3W_DI_DO = 2,
    SPI_MODE_LCD_4W_DIO   = 3,
    SPI_MODE_LCD_4W_DI_DO = 4,
    SPI_MODE_MAX
} SPI_MODE_T;

typedef struct {
    SPI_CSN_T cs;
    SPI_DC_T dc;
    SPI_MODE_T spi_mode;
    uint32_t clk_freq;
    uint16_t clk_mode;
    uint8_t bit_cnt;
    uint8_t gpa_idx_cs;
} spim_cfg_t;

/**
 * Functions
 */

/**
 * @brief: Initialize SPI master
 */
void spim_init(spim_cfg_t *spim);

/**
 * @brief: SPI master write byte value
 */
void spim_write_byte(spim_cfg_t *spim, uint8_t byte_val, bool cs_end);

/**
 * @brief: SPI master read byte value
 * @return: Pwrkey enable or not
 */
uint8_t spim_read_byte(spim_cfg_t *spim, bool cs_end);
/* ------------------------------------------------------- */


/* ------------------ spi api version 2 ------------------ */
#define SPI0_DMA_BYTE_CNT_MAX   (64)
#define SPI0_3_WIRE_ENABLED     (0)

// spi_cfg
#define SPI_CLK_FREQ            (1000000)

// spi_mode
#define SPI0_MODE_MASTER        1
#define SPI0_MODE_SLAVE         0

// spi_dma
#define SPI0_ISR_ENABLED        (1 && defined(CFG_RTOS))
#define SPI0_DMA_ISR_ENABLED    (1 && defined(CFG_RTOS))

void spi0_init(uint8_t spi_mode);
void spi0_deinit(void);
int spi0_slave_write_read_words(unsigned int *buff_wr, unsigned int word_cnt_wr,
                                unsigned int *buff_rd, unsigned int word_cnt_rd);
int spi0_slave_write_words(unsigned int *buff_wr, unsigned int word_cnt_wr);
int spi0_slave_read_words(unsigned int *buff_rd, unsigned int word_cnt_rd);
int spi0_dma_read_bytes(unsigned char *buff_rd, unsigned int byte_cnt_rd);
int spi0_dma_write_bytes(unsigned char *buff_wr, unsigned int byte_cnt);
int spi0_dma_write_words(unsigned int *buff_wr, unsigned int word_cnt);
/* ------------------------------------------------------- */

#endif /* _SPI_API_H_ */
