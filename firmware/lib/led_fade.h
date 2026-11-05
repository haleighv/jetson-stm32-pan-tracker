/*
 * led_fade.h
 *
 * Brightness for a "breathing" LED: off -> full -> off over one period,
 * squared so it looks even to the eye (perceived brightness isn't linear).
 * Portable, host-tested; the LED task turns the result into software PWM.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef LED_FADE_H
#define LED_FADE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duty in PWM steps (0 = off, steps = fully on) at phase_ms into a
 * period_ms cycle. phase_ms wraps. Returns 0 if period_ms < 2. */
uint8_t led_fade_duty(uint32_t phase_ms, uint32_t period_ms, uint8_t steps);

#ifdef __cplusplus
}
#endif

#endif /* LED_FADE_H */
