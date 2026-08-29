/**
 * aicwf_gpio_api.c
 *
 * rftest cmd declarations
 *
 * Copyright (C) AICSemi 2018-2025
 */

#ifndef _AICWF_GPIO_API_H_
#define _AICWF_GPIO_API_H_

enum {
    GPIO_TYPEA = 0,
    GPIO_TYPEB = 1,
};

enum {
    GPIODIR_INPUT  = 0,
    GPIODIR_OUTPUT = 1,
};

enum {
    GPIOVAL_LOW  = 0,
    GPIOVAL_HIGH = 1,
};

void gpioa_init(struct rwnx_hw *rwnx_hw, int gpidx);
void gpioa_deinit(struct rwnx_hw *rwnx_hw, int gpidx);
void gpioa_dir_in(struct rwnx_hw *rwnx_hw, int gpidx);
void gpioa_dir_out(struct rwnx_hw *rwnx_hw, int gpidx);
void gpioa_set(struct rwnx_hw *rwnx_hw, int gpidx);
void gpioa_clr(struct rwnx_hw *rwnx_hw, int gpidx);
int gpioa_get(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_init(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_deinit(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_dir_in(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_dir_out(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_set(struct rwnx_hw *rwnx_hw, int gpidx);
void gpiob_clr(struct rwnx_hw *rwnx_hw, int gpidx);
int gpiob_get(struct rwnx_hw *rwnx_hw, int gpidx);

#endif /* _AICWF_GPIO_API_H_ */
