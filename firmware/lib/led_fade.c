/*
 * led_fade.c
 *
 * See led_fade.h.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "led_fade.h"

uint8_t led_fade_duty(uint32_t phase_ms, uint32_t period_ms, uint8_t steps)
{
    if (period_ms < 2 || steps == 0) {
        return 0;
    }

    uint32_t half = period_ms / 2;
    uint32_t p    = phase_ms % period_ms;

    /* Triangle: distance from the nearest "off" end, 0..half */
    uint32_t x = (p < half) ? p : period_ms - p;
    if (x > half) {
        x = half;
    }

    /* Square it, scale to steps, round to nearest */
    uint64_t num = (uint64_t)x * x * steps;
    uint64_t den = (uint64_t)half * half;
    return (uint8_t)((num + den / 2) / den);
}
