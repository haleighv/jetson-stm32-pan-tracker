/*
 * task_led.c
 *
 * LED task. Subscribes to EVENT_LED_CMD on the message bus and drives LD1-LD3:
 * - on/off: pin written right away
 * - blink:  one auto-reload software timer per LED, toggles every period/2
 * - fade:   one shared 1 ms software timer doing software PWM for every
 *           fading LED; started on the first fade, stopped after the last
 *
 * Timer callbacks run in the FreeRTOS timer task, not here. A callback can
 * still fire once after the mode changed (an expiry that was already due),
 * so callbacks check the LED's current mode, and mode changes plus pin
 * writes happen inside short critical sections. A stale callback is then
 * a no-op instead of fighting the new mode.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "led"
#include "task_led.h"
#include "log.h"
#include "msg.h"
#include "led_fade.h"
#include "task_utilities.h"

#include <stdint.h>
#include <strings.h>    /* strcasecmp */

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "main.h"

#define FADE_PWM_STEPS  10      /* brightness levels; PWM frame = 10 ticks = 100 Hz */

typedef struct {
    GPIO_TypeDef        *port;
    uint16_t             pin;
    volatile led_mode_t  mode;
    uint16_t             period_ms;
    uint32_t             phase_ms;      /* fade position: reset by apply(), advanced by fade_cb() */
    TimerHandle_t        blink_timer;
} led_state_t;

static led_state_t leds[LED_COUNT] = {
    [LED_GREEN] = { .port = LD1_GPIO_Port, .pin = LD1_Pin },
    [LED_AMBER] = { .port = LD2_GPIO_Port, .pin = LD2_Pin },
    [LED_RED]   = { .port = LD3_GPIO_Port, .pin = LD3_Pin },
};

static TimerHandle_t fade_timer;
static uint8_t       pwm_count;         /* timer task only */
static msg_queue_id_t led_msg_queue_id;

static const char *const mode_names[LED_MODE_COUNT] = { "off", "on", "blink", "fade" };

/* --- Public --- */

bool led_set(led_id_t led, led_mode_t mode, uint16_t period_ms)
{
    if (led >= LED_COUNT || mode >= LED_MODE_COUNT) {
        return false;
    }
    msg_t m = {
        .id = EVENT_LED_CMD,
        .data.led = { .led = (uint8_t)led, .mode = (uint8_t)mode, .period_ms = period_ms },
    };
    return msg_post(&m, 0);
}

const char *led_mode_name(led_mode_t mode)
{
    return (mode < LED_MODE_COUNT) ? mode_names[mode] : "?";
}

bool led_mode_from_str(const char *s, led_mode_t *out)
{
    for (int i = 0; i < LED_MODE_COUNT; i++) {
        if (strcasecmp(s, mode_names[i]) == 0) {
            *out = (led_mode_t)i;
            return true;
        }
    }
    return false;
}

/* --- Timer callbacks (timer task context, keep short, never block) --- */

static void blink_cb(TimerHandle_t t)
{
    led_state_t *l = &leds[(uintptr_t)pvTimerGetTimerID(t)];

    taskENTER_CRITICAL();
    if (l->mode == LED_MODE_BLINK) {
        HAL_GPIO_TogglePin(l->port, l->pin);
    }
    taskEXIT_CRITICAL();
}

static void fade_cb(TimerHandle_t t)
{
    (void)t;
    pwm_count = (uint8_t)((pwm_count + 1) % FADE_PWM_STEPS);

    for (int i = 0; i < LED_COUNT; i++) {
        led_state_t *l = &leds[i];

        taskENTER_CRITICAL();
        if (l->mode == LED_MODE_FADE) {
            l->phase_ms = (l->phase_ms + portTICK_PERIOD_MS) % l->period_ms;
            uint8_t duty = led_fade_duty(l->phase_ms, l->period_ms, FADE_PWM_STEPS);
            HAL_GPIO_WritePin(l->port, l->pin, (pwm_count < duty) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
        taskEXIT_CRITICAL();
    }
}

/* --- Task --- */

static bool any_fading(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        if (leds[i].mode == LED_MODE_FADE) {
            return true;
        }
    }
    return false;
}

static void apply(const msg_led_t *cmd)
{
    if (cmd->led >= LED_COUNT || cmd->mode >= LED_MODE_COUNT) {
        LOG_W("bad command led %u mode %u", cmd->led, cmd->mode);
        return;
    }

    led_state_t *l    = &leds[cmd->led];
    led_mode_t   mode = (led_mode_t)cmd->mode;
    uint16_t     period = cmd->period_ms;

    if (period == 0) {
        period = (mode == LED_MODE_FADE) ? LED_FADE_DEFAULT_MS : LED_BLINK_DEFAULT_MS;
    }
    if (period < 2 * portTICK_PERIOD_MS) {
        period = 2 * portTICK_PERIOD_MS;    /* blink needs at least 1 tick per half */
    }

    /* Switch mode and set the starting pin level in one step, so a timer
     * callback sees either the old setup or the new one, never half. */
    taskENTER_CRITICAL();
    l->mode      = mode;
    l->period_ms = period;
    l->phase_ms  = 0;
    HAL_GPIO_WritePin(l->port, l->pin, (mode == LED_MODE_ON) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    taskEXIT_CRITICAL();

    /* Timers: commands go through the timer task's queue, so don't wait */
    BaseType_t ok = pdPASS;
    if (mode == LED_MODE_BLINK) {
        ok = xTimerChangePeriod(l->blink_timer, pdMS_TO_TICKS(period / 2), 0);  /* also starts it */
    } else if (xTimerIsTimerActive(l->blink_timer) != pdFALSE) {
        ok = xTimerStop(l->blink_timer, 0);
    }

    /* Fade timer runs only while something is fading (1 kHz otherwise wasted) */
    bool fade_running = (xTimerIsTimerActive(fade_timer) != pdFALSE);
    if (any_fading() && !fade_running) {
        ok &= xTimerStart(fade_timer, 0);
    } else if (!any_fading() && fade_running) {
        ok &= xTimerStop(fade_timer, 0);
    }

    if (ok != pdPASS) {
        LOG_W("timer command queue full");
    }
    LOG_D("led %u %s %u ms", cmd->led + 1, led_mode_name(mode), period);
}

static void initialize(void)
{
    event_id_t event_table[] = {
        EVENT_LED_CMD,
    };
    /* Initialize LED timers and pins */
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i].blink_timer = xTimerCreate("led_blink", pdMS_TO_TICKS(LED_BLINK_DEFAULT_MS / 2),
                                           pdTRUE, (void *)(uintptr_t)i, blink_cb);
        configASSERT(leds[i].blink_timer != NULL);
        HAL_GPIO_WritePin(leds[i].port, leds[i].pin, GPIO_PIN_RESET);
    }
    fade_timer = xTimerCreate("led_fade", 1, pdTRUE, NULL, fade_cb);
    configASSERT(fade_timer != NULL);

    /* Subscribe to messages */
    led_msg_queue_id = msg_subscriber_create("led", 8);
    configASSERT(led_msg_queue_id != MSG_QUEUE_ID_INVALID);
    for (size_t i = 0; i < sizeof(event_table) / sizeof(event_table[0]); i++) {
        configASSERT(msg_subscribe(led_msg_queue_id, event_table[i]));
    }
}

void task_led(void *arg)
{
    (void)arg;

    initialize();
    task_sync();

    msg_t message;
    for (;;) {
        if (!msg_recv(led_msg_queue_id, &message, portMAX_DELAY)) {
            continue;
        }
        if (message.id == EVENT_LED_CMD) {
            apply(&message.data.led);
        } else {
            LOG_W("unexpected event %u", message.id);
        }
    }
}
