/*
 * msg.h
 *
 * Publish/subscribe message bus. Any task (or ISR) posts a message to one
 * global queue. The msg task (the dispatcher) takes each message off it and
 * copies it to every task that subscribed to that event id.
 *
 * Notes:
 * - Every message has the same shape: an event id (events.h) plus a small
 *   payload, 12 bytes, copied by value.
 * - A subscriber still gets its own delivery queue to block on: a queue
 *   can only have one reader, otherwise each message goes to whichever
 *   task wakes first. Subscribers never touch the global queue.
 * - Subscribe at the top of the task, before its loop. Messages posted
 *   before anyone subscribes are counted as unrouted and dropped.
 * - Never blocks unless asked to. A full global queue or a full delivery
 *   queue drops the message and counts it (see msg_get_stats()).
 * - The dispatcher runs at the highest task priority so routing adds
 *   little delay. It does no work besides copying.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef MSG_H
#define MSG_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MSG_QUEUE_DEPTH     32      /* global queue, in messages */
#define MSG_MAX_SUBSCRIBERS 8       /* subscribers are bits in a uint32_t mask */

/* Payload for EVENT_LED_CMD */
typedef struct {
    uint8_t  led;           /* led_id_t */
    uint8_t  mode;          /* led_mode_t */
    uint16_t period_ms;     /* blink/fade period, 0 = default */
} msg_led_t;

typedef struct {
    uint16_t id;            /* event_id_t */
    uint16_t reserved;
    union {
        msg_led_t led;
        uint32_t  u32[2];
        int32_t   i32[2];
    } data;
} msg_t;

/* ID of a subscriber's delivery queue. MSG_QUEUE_ID_INVALID if creation failed. */
typedef int8_t msg_queue_id_t;
#define MSG_QUEUE_ID_INVALID     ((msg_queue_id_t)-1)

/* Counters since boot, for the CLI */
typedef struct {
    uint32_t posted;            /* accepted onto the global queue */
    uint32_t post_dropped;      /* global queue full */
    uint32_t unrouted;          /* no subscriber for that event */
    uint8_t  num_subs;
} msg_stats_t;

/* Create the global queue. Once, in init_tasks(), before creating tasks. */
void msg_init(void);

/* Dispatcher task. Goes in the task table at the highest task priority. */
void task_msg(void *arg);

/* Register the calling task as a subscriber with its own delivery queue
 * of depth messages. Call once at task start. */
msg_queue_id_t msg_subscriber_create(const char *name, uint8_t depth);

/* Ask for every message with this event id. Call at task start. */
bool msg_subscribe(msg_queue_id_t queue_id, event_id_t id);

/* Post to the bus. Task context. timeout 0 = never wait. */
bool msg_post(const msg_t *m, TickType_t timeout);

/* Post from an ISR. Pass woken to portYIELD_FROM_ISR() afterwards. */
bool msg_post_from_isr(const msg_t *m, BaseType_t *woken);

/* Subscriber only: wait for its next message. */
bool msg_recv(msg_queue_id_t self, msg_t *m, TickType_t timeout);

void        msg_get_stats(msg_stats_t *out);
const char *msg_queue_name(msg_queue_id_t queue_id);
uint32_t    msg_queue_dropped(msg_queue_id_t queue_id);     /* delivery queue full */

#ifdef __cplusplus
}
#endif

#endif /* MSG_H */
