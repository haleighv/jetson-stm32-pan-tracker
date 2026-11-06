/*
 * control.c
 *
 * 1 kHz control tick on TIM6, plus period stats from the DWT cycle counter.
 * See control.h.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "control"
#include "control.h"
#include "log.h"

#include <stdbool.h>

#include "main.h"
#include "tim.h"

/* ISR only */
static uint32_t last_cyc;
static bool     have_last;

/* Written by the ISR, read by tasks */
static volatile uint32_t period_min_cyc = UINT32_MAX;
static volatile uint32_t period_max_cyc = 0;
static volatile uint32_t period_min_tick;   /* tick_count when min was recorded */
static volatile uint32_t period_max_tick;
static volatile uint32_t tick_count = 0;

/* Written by tasks, read and cleared by the ISR */
static volatile bool reset_requested = true;

/* --- ISR --- */

/* TIM6 period elapsed. Registered with HAL_TIM_RegisterCallback(), so
 * main.c stays untouched. Keep it short: no logging, nothing that waits.
 *
 * NO FREERTOS CALLS IN HERE, not even the ...FromISR() ones. TIM6 runs at
 * NVIC priority 4, above configMAX_SYSCALL_INTERRUPT_PRIORITY (5), so the
 * kernel's critical sections can't delay it. The flip side: the kernel
 * can't protect itself from it either, so any FreeRTOS call from here can
 * corrupt the scheduler (configASSERT catches it in the port). To hand
 * data to a task, write a variable the task polls (single writer, as
 * below), or move the work to a lower-priority interrupt. */
static void control_tick_isr(TIM_HandleTypeDef *htim)
{
    (void)htim;

    uint32_t now_cyc    = DWT->CYCCNT;
    uint32_t period_cyc = now_cyc - last_cyc;   /* unsigned, so counter wrap is fine */

    if (reset_requested) {
        period_min_cyc  = UINT32_MAX;
        period_max_cyc  = 0;
        reset_requested = false;
    } else if (have_last) {
        if (period_cyc < period_min_cyc) {
            period_min_cyc  = period_cyc;
            period_min_tick = tick_count;
        }
        if (period_cyc > period_max_cyc) {
            period_max_cyc  = period_cyc;
            period_max_tick = tick_count;
        }
    }

    /* The first tick has no previous sample to measure from */
    have_last = true;
    last_cyc  = now_cyc;
    tick_count++;

    /* Scope pin (PG14 / D2): one edge per tick */
    HAL_GPIO_TogglePin(SCOPE_GPIO_Port, SCOPE_Pin);
}

/* --- Public --- */

void control_start(void)
{
    /* DWT cycle counter first, so the first tick reads a running counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;     /* turn on the trace/debug block */
    DWT->LAR = 0xC5ACCE55;                              /* unlock (needed on some Cortex-M7s) */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;                /* start counting */

    /* Register before starting: HAL only accepts callbacks while the timer
     * is READY, and Start_IT moves it to BUSY */
    if (HAL_TIM_RegisterCallback(&htim6, HAL_TIM_PERIOD_ELAPSED_CB_ID, control_tick_isr) != HAL_OK ||
        HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
        LOG_E("tim6 start failed");
        return;
    }
    LOG_I("control tick started");
}

uint32_t control_tick_count(void)
{
    return tick_count;
}

void control_get_period(uint32_t *min_cyc, uint32_t *max_cyc)
{
    if (min_cyc == NULL || max_cyc == NULL) {
        return;
    }
    *min_cyc = period_min_cyc;
    *max_cyc = period_max_cyc;
}

void control_get_period_ticks(uint32_t *min_tick, uint32_t *max_tick)
{
    if (min_tick == NULL || max_tick == NULL) {
        return;
    }
    *min_tick = period_min_tick;
    *max_tick = period_max_tick;
}

void control_reset_period(void)
{
    reset_requested = true;
}
