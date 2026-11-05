/*
 * events.h
 *
 * Event defines
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef EVENTS_H
#define EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Event identifiers. Extend as new firmware features are added. */
typedef enum {
    EVENT_NONE = 0,
    EVENT_BOOT,
    EVENT_BUTTON_PRESSED,
    EVENT_ENCODER_TICK,
    EVENT_TARGET_UPDATED,
    EVENT_MOTOR_START,
    EVENT_MOTOR_STOP,
    EVENT_LIMIT_REACHED,
    EVENT_ERROR,
    EVENT_LED_CMD,          /* data.led: set an LED's mode (task_led) */
    EVENT_COUNT
} event_id_t;


#ifdef __cplusplus
}
#endif

#endif /* EVENTS_H */
