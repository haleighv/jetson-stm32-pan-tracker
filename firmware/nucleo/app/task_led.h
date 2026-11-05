/*
 * task_led.h
 *
 * LED task: the only code that drives the user LEDs. Other tasks ask for a
 * mode (off, on, blink, fade) by message and the LED task does the timing
 * with FreeRTOS software timers.
 *
 * Notes:
 * - led_set() and the helpers post EVENT_LED_CMD to the message bus and
 *   never block. They return false if the bus queue is full.
 * - Task context only. From an ISR, build a msg_t and use msg_post_from_isr().
 * - Fade is software PWM from a 1 ms software timer: 10 brightness steps at
 *   100 Hz, so it's a little steppy. The timer only runs while an LED is
 *   fading. Hardware PWM would be smoother but needs timer pins.
 * - Exception: fatal paths (stack overflow hook, HardFault) may write LD3
 *   directly, since the LED task can't run at that point.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef TASK_LED_H
#define TASK_LED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nucleo user LEDs */
typedef enum {
    LED_GREEN = 0,          /* LD1, PB0 */
    LED_AMBER,              /* LD2, PE1 */
    LED_RED,                /* LD3, PB14 */
    LED_COUNT
} led_id_t;

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_BLINK,         /* 50% duty, period_ms per on+off cycle */
    LED_MODE_FADE,          /* off -> full -> off over period_ms */
    LED_MODE_COUNT
} led_mode_t;

#define LED_BLINK_DEFAULT_MS    500
#define LED_FADE_DEFAULT_MS     2000

void task_led(void *arg);

/* Ask the LED task for a mode. period_ms 0 = default for that mode. */
bool led_set(led_id_t led, led_mode_t mode, uint16_t period_ms);

static inline bool led_on(led_id_t led)                     { return led_set(led, LED_MODE_ON, 0); }
static inline bool led_off(led_id_t led)                    { return led_set(led, LED_MODE_OFF, 0); }
static inline bool led_blink(led_id_t led, uint16_t ms)     { return led_set(led, LED_MODE_BLINK, ms); }
static inline bool led_fade(led_id_t led, uint16_t ms)      { return led_set(led, LED_MODE_FADE, ms); }

/* "off", "on", "blink", "fade" and back, for the CLI */
const char *led_mode_name(led_mode_t mode);
bool        led_mode_from_str(const char *s, led_mode_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TASK_LED_H */
