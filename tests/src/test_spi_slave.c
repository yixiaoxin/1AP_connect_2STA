/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
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
#include "dbg.h"
#include "spi_api.h"
#include "console.h"

/*
 ****************************************************************************************
 * This spi slave interface test is only for AIC8800MC and AIC8800M40.
 ****************************************************************************************
 */

#ifdef CFG_TEST_SPI_SLAVE
/*
 * MACROS
 ****************************************************************************************
 */
#define USER_PRINTF             dbg
#define SPI_MASTER_OR_SLAVE     (0)  // 1: master mode, 0: slave mode
#define BUFFER_LENGTH           (16)

/*
 * GLOBAL CONSTANTS & VARIABLES
 ****************************************************************************************
 */
unsigned int tx_buff[BUFFER_LENGTH];
unsigned int rx_buff[BUFFER_LENGTH];

/*
 * FUNCTIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief init spi, config iomux & clock div
 ****************************************************************************************
 */

// master
int do_master_cmd(int argc, char * const argv[])
{
    #if PLF_AIC8800M40
    //spi0_dma_write_read_words(tx_buff, BUFFER_LENGTH, rx_buff, BUFFER_LENGTH, 1);
    spi0_dma_write_read_16bit(tx_buff, BUFFER_LENGTH, rx_buff, BUFFER_LENGTH, 1);
    #endif

    USER_PRINTF("\nmaster buf_rd:\n");
    for (int i = 0; i < BUFFER_LENGTH; i++) {
        USER_PRINTF(" %08x", rx_buff[i]);
        if ((i+1)%8 == 0 && i != 0)
            dbg("\n");
    }
    USER_PRINTF("\nspi master trans test done\n");
    return 0;
}

//slave
int do_slave_cmd(int argc, char * const argv[])
{
    #if PLF_AIC8800MC
    /*  No DMA write and read interface  */
    spi0_slave_write_read_words(tx_buff, BUFFER_LENGTH, rx_buff, BUFFER_LENGTH);
    //spi0_slave_write_words(tx_buff, BUFFER_LENGTH);
    //spi0_slave_read_words(rx_buff, BUFFER_LENGTH);
    #endif

    #if PLF_AIC8800M40
    /*  DMA word write and read interface  */
    //spi0_dma_write_read_words(tx_buff, BUFFER_LENGTH, rx_buff, BUFFER_LENGTH, 0);
    //spi0_slave_dma_write_words(tx_buff, BUFFER_LENGTH);
    //spi0_slave_dma_read_words(rx_buff, BUFFER_LENGTH);
    /*  DMA 16bit write and read interface  */
    spi0_dma_write_read_16bit(tx_buff, BUFFER_LENGTH, rx_buff, BUFFER_LENGTH, 0);
    //spi0_slave_dma_write_16bit(tx_buff, BUFFER_LENGTH);
    //spi0_slave_dma_read_16bit(rx_buff, BUFFER_LENGTH);
    /*  No DMA write and read interface  */
    //spi0_slave_write_words(tx_buff, BUFFER_LENGTH);
    //spi0_slave_read_words(rx_buff, BUFFER_LENGTH);
    #endif

    USER_PRINTF("\nslave buf_rd:\n");
    for (int i = 0; i < BUFFER_LENGTH; i++) {
        USER_PRINTF(" %08x", rx_buff[i]);
        if ((i+1)%8 == 0 && i != 0)
            dbg("\n");
    }
    USER_PRINTF("\nspi slave trans test done\n");
    return 0;
}

/**
 ****************************************************************************************
 * @brief test task implementation.
 ****************************************************************************************
 */
void spi_slave_test(void)
{
    USER_PRINTF("\nStart test case: spi trans\n\n");

    /* prepare data to write */
    USER_PRINTF("tx_buff:\n");
    for(int i=0; i<BUFFER_LENGTH; i++) {
        rx_buff[i] = 0;
        #if SPI_MASTER_OR_SLAVE
        tx_buff[i] = 3<<24 | 3<<16 | 3<<8 | (i&0xff);
        #else
        tx_buff[i] = 1<<24 | 1<<16 | 1<<8 | (i&0xff);
        #endif
        USER_PRINTF(" %08x", tx_buff[i]);
        if ((i+1)%8 == 0 && i != 0)
            dbg("\n");
    }

    /* add console */
    #if SPI_MASTER_OR_SLAVE
    dbg("\nSPI_MASTER_MODE\n");
    console_cmd_add("ms_cmd", "master test cmd", 1,  1, do_master_cmd);
    spi0_init(SPI0_MODE_MASTER);
    #else
    dbg("\nSPI_SLAVE_MODE\n");
    console_cmd_add("sl_cmd", "slave test cmd", 1,  1, do_slave_cmd);
    spi0_init(SPI0_MODE_SLAVE);
    #endif
}

#endif /* CFG_TEST_SPI_SLAVE */
