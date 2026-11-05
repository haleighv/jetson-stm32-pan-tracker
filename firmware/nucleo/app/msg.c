/*
 * msg.c
 *
 * Message bus: global queue, subscription table, dispatcher task. See msg.h.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "msg"
#include "msg.h"
#include "log.h"
#include "task_utilities.h"

#include "task.h"
#include "queue.h"

_Static_assert(sizeof(msg_t) == 12, "msg_t grew; check queue RAM");
_Static_assert(MSG_MAX_SUBSCRIBERS <= 32, "subscribers are bits in a uint32_t");

typedef struct {
    const char       *name;
    QueueHandle_t     queue;
    volatile uint32_t dropped;
} subscriber_t;

static QueueHandle_t      bus;
static subscriber_t       subs[MSG_MAX_SUBSCRIBERS];
static volatile uint8_t   num_subs;

/* sub_mask[id] bit n set = subscriber n wants event id */
static volatile uint32_t  sub_mask[EVENT_COUNT];

static volatile uint32_t  posted;
static volatile uint32_t  post_dropped;
static volatile uint32_t  unrouted;

/* --- Setup --- */

void msg_init(void)
{
    bus = xQueueCreate(MSG_QUEUE_DEPTH, sizeof(msg_t));
    configASSERT(bus != NULL);
    vQueueAddToRegistry(bus, "msg_bus");    /* names it in the debugger */
}

msg_queue_id_t msg_subscriber_create(const char *name, uint8_t depth)
{
    QueueHandle_t q = xQueueCreate(depth, sizeof(msg_t));
    if (q == NULL) {
        LOG_E("no heap for %s queue", name);
        return MSG_QUEUE_ID_INVALID;
    }
    vQueueAddToRegistry(q, name);

    msg_queue_id_t id = MSG_QUEUE_ID_INVALID;
    taskENTER_CRITICAL();
    if (num_subs < MSG_MAX_SUBSCRIBERS) {
        id = (msg_queue_id_t)num_subs;
        subs[id].name = name;
        subs[id].queue = q;
        num_subs++;             /* last, so the dispatcher never sees a half-set entry */
    }
    taskEXIT_CRITICAL();

    if (id == MSG_QUEUE_ID_INVALID) {
        LOG_E("too many subscribers, %s not added", name);
        vQueueDelete(q);
    }
    return id;
}

bool msg_subscribe(msg_queue_id_t queue_id, event_id_t id)
{
    if (queue_id < 0 || queue_id >= (msg_queue_id_t)num_subs || id >= EVENT_COUNT) {
        return false;
    }
    taskENTER_CRITICAL();
    sub_mask[id] |= (1UL << queue_id);
    taskEXIT_CRITICAL();
    return true;
}

/* --- Posting --- */

bool msg_post(const msg_t *m, TickType_t timeout)
{
    if (bus == NULL || m == NULL || m->id >= EVENT_COUNT) {
        return false;
    }
    bool ok = (xQueueSend(bus, m, timeout) == pdPASS);

    taskENTER_CRITICAL();
    if (ok) {
        posted++;
    } else {
        post_dropped++;
    }
    taskEXIT_CRITICAL();
    return ok;
}

bool msg_post_from_isr(const msg_t *m, BaseType_t *woken)
{
    if (bus == NULL || m == NULL || m->id >= EVENT_COUNT) {
        return false;
    }
    bool ok = (xQueueSendFromISR(bus, m, woken) == pdPASS);

    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    if (ok) {
        posted++;
    } else {
        post_dropped++;
    }
    taskEXIT_CRITICAL_FROM_ISR(saved);
    return ok;
}

/* --- Receiving --- */

bool msg_recv(msg_queue_id_t self, msg_t *m, TickType_t timeout)
{
    if (self < 0 || self >= (msg_queue_id_t)num_subs || m == NULL) {
        return false;
    }
    return xQueueReceive(subs[self].queue, m, timeout) == pdPASS;
}

/* --- Dispatcher --- */

static void initialize(void)
{
    configASSERT(bus != NULL);      /* msg_init() must run first */
}

void task_msg(void *arg)
{
    (void)arg;

    /* Syncing means routing starts only after every task has subscribed,
     * so nothing posted at boot ends up unrouted. */
    initialize();
    task_sync();

    msg_t m;
    for (;;) {
        if (xQueueReceive(bus, &m, portMAX_DELAY) != pdPASS) {
            continue;
        }

        uint32_t mask = sub_mask[m.id];
        if (mask == 0) {
            unrouted++;             /* only this task writes it */
            continue;
        }

        /* Copy to each subscriber. Never wait: one stuck subscriber must
         * not hold up delivery to the others. */
        for (uint8_t i = 0; i < num_subs; i++) {
            if ((mask & (1UL << i)) == 0) {
                continue;
            }
            if (xQueueSend(subs[i].queue, &m, 0) != pdPASS) {
                subs[i].dropped++;  /* only this task writes it */
            }
        }
    }
}

/* --- Stats --- */

void msg_get_stats(msg_stats_t *out)
{
    taskENTER_CRITICAL();
    out->posted       = posted;
    out->post_dropped = post_dropped;
    out->unrouted     = unrouted;
    out->num_subs     = num_subs;
    taskEXIT_CRITICAL();
}

const char *msg_queue_name(msg_queue_id_t queue_id)
{
    return (queue_id >= 0 && queue_id < (msg_queue_id_t)num_subs) ? subs[queue_id].name : "?";
}

uint32_t msg_queue_dropped(msg_queue_id_t queue_id)
{
    return (queue_id >= 0 && queue_id < (msg_queue_id_t)num_subs) ? subs[queue_id].dropped : 0;
}
