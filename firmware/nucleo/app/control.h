/*
 * control.h
 *
 * Motor control loop. For now it's the 1 kHz control tick on TIM6 and its
 * timing stats; encoder -> PID -> PWM moves in here later.
 *
 * Notes:
 * - The tick runs in the TIM6 interrupt at NVIC priority 4, above the
 *   FreeRTOS syscall limit (5), so its timing doesn't depend on the
 *   scheduler or kernel critical sections. It can't call FreeRTOS at all.
 * - The ISR is the only writer of the counters below; tasks only read them.
 * - Periods are measured with the DWT cycle counter, in CPU cycles.
 *   Ideal is SystemCoreClock / 1000 (520,000 at 520 MHz).
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turn on the DWT cycle counter, then start the TIM6 tick. Call once from
 * the supervisor after task_sync(). */
void control_start(void);

/* Ticks since control_start(). The supervisor watches it to know the
 * loop is alive. */
uint32_t control_tick_count(void);

/* Shortest and longest time between ticks, in CPU cycles, since the last
 * reset. min is UINT32_MAX until the first full period. */
void control_get_period(uint32_t *min_cyc, uint32_t *max_cyc);

/* Tick number (since control_start(), 1 tick = 1 ms) when the current min
 * and max were recorded. Useful for lining an outlier up with the log. */
void control_get_period_ticks(uint32_t *min_tick, uint32_t *max_tick);

/* Ask the ISR to restart min/max on its next tick. Only sets a flag, so
 * the ISR stays the only writer. */
void control_reset_period(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_H */
