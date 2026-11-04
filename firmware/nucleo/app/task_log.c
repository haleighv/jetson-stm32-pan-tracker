/*
 * task_log.c
 *
 * Log core and log task. log_write() formats into a fixed-size message and
 * queues it without blocking. The log task is the only code that hands
 * messages to sinks (uart, flash, udp), so sinks never need locking.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "log"
#include "log.h"
#include "task_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>    /* strcasecmp */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Special level value for the flush marker. Never a real message level. */
#define LOG_LVL_FLUSH   LOG_LVL_COUNT

/* One queue item. Copied in by value, so the caller's stack buffer can go
 * away as soon as log_write() returns. */
typedef struct {
    uint8_t  level;
    uint8_t  len;                   /* bytes in text, not NUL-terminated */
    char     text[LOG_MSG_MAX];
} log_msg_t;

_Static_assert(LOG_MSG_MAX <= 255, "log_msg_t.len is a uint8_t");

static QueueHandle_t      log_queue;
static SemaphoreHandle_t  flush_done;

static log_sink_t        *sinks[LOG_MAX_SINKS];
static bool               sink_ready[LOG_MAX_SINKS];   /* init() has run */
static volatile uint8_t   sink_count;

static volatile log_level_t log_level = LOG_LVL_DEBUG;
static volatile uint32_t    dropped;

static const char  level_char[LOG_LVL_COUNT]  = { 'E', 'W', 'I', 'D' };
static const char *level_names[LOG_LVL_COUNT] = { "ERROR", "WARN", "INFO", "DEBUG" };

/* --- Sinks --- */

bool log_sink_register(log_sink_t *sink)
{
    bool ok = false;

    if (sink == NULL || sink->write == NULL) {
        return false;
    }

    taskENTER_CRITICAL();
    if (sink_count < LOG_MAX_SINKS) {
        sinks[sink_count] = sink;
        sink_ready[sink_count] = false;     /* log task runs init() on first use */
        sink_count++;
        ok = true;
    }
    taskEXIT_CRITICAL();

    return ok;
}

log_sink_t *log_sink_find(const char *name)
{
    for (uint8_t i = 0; i < sink_count; i++) {
        if (strcmp(sinks[i]->name, name) == 0) {
            return sinks[i];
        }
    }
    return NULL;
}

/* Log task only. Runs a sink's init() the first time it's used, so sinks
 * registered late (udp, once the link is up) get initialized too. */
static void sinks_write(log_level_t lvl, const char *msg, size_t len)
{
    for (uint8_t i = 0; i < sink_count; i++) {
        log_sink_t *s = sinks[i];

        if (!sink_ready[i]) {
            sink_ready[i] = true;
            if (s->init != NULL && !s->init()) {
                s->enabled = false;
            }
        }
        if (s->enabled && lvl <= s->min_level) {
            s->write(msg, len);
        }
    }
}

static void sinks_flush(void)
{
    for (uint8_t i = 0; i < sink_count; i++) {
        if (sinks[i]->enabled && sinks[i]->flush != NULL) {
            sinks[i]->flush();
        }
    }
}

/* --- Core --- */

void log_init(void)
{
    log_queue  = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_msg_t));
    flush_done = xSemaphoreCreateBinary();
    configASSERT(log_queue != NULL);
    configASSERT(flush_done != NULL);
}

void log_set_level(log_level_t lvl)
{
    if (lvl < LOG_LVL_COUNT) {
        log_level = lvl;
    }
}

log_level_t log_get_level(void)
{
    return log_level;
}

uint32_t log_dropped_count(void)
{
    return dropped;
}

void log_write(log_level_t lvl, const char *tag, const char *fmt, ...)
{
    if (lvl >= LOG_LVL_COUNT || lvl > log_level) {
        return;
    }

    /* Formatting is too slow and stack-hungry for an ISR */
    configASSERT(!xPortIsInsideInterrupt());

    log_msg_t msg;
    msg.level = (uint8_t)lvl;

    /* Leave room for "\r\n" after the text */
    const size_t cap = sizeof(msg.text) - 2;

    uint32_t ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    int n = snprintf(msg.text, cap, "[%5lu.%03lu] %c %s: ",
                     (unsigned long)(ms / 1000U), (unsigned long)(ms % 1000U),
                     level_char[lvl], tag);
    if (n < 0) {
        return;
    }
    size_t len = ((size_t)n < cap) ? (size_t)n : cap - 1;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(msg.text + len, cap - len, fmt, ap);
    va_end(ap);
    if (m > 0) {
        len += (size_t)m;
    }
    if (len > cap - 1) {
        len = cap - 1;      /* truncated */
    }

    msg.text[len++] = '\r';
    msg.text[len++] = '\n';
    msg.len = (uint8_t)len;

    /* Before log_init(): no queue yet, print directly */
    if (log_queue == NULL) {
        printf("%.*s", (int)msg.len, msg.text);
        return;
    }

    /* Never wait. A full queue drops the message and counts it. */
    if (xQueueSend(log_queue, &msg, 0) != pdPASS) {
        taskENTER_CRITICAL();
        dropped++;
        taskEXIT_CRITICAL();
    }
}

void log_flush(void)
{
    if (log_queue == NULL) {
        return;
    }

    /* Queue a marker behind everything already queued, then wait for the
     * log task to reach it and flush the sinks. Bounded waits so a stuck
     * sink can't hang the caller. */
    log_msg_t marker = { .level = LOG_LVL_FLUSH, .len = 0 };
    if (xQueueSend(log_queue, &marker, pdMS_TO_TICKS(100)) == pdPASS) {
        (void)xSemaphoreTake(flush_done, pdMS_TO_TICKS(500));
    }
}

const char *log_level_name(log_level_t lvl)
{
    return (lvl < LOG_LVL_COUNT) ? level_names[lvl] : "?";
}

bool log_level_from_str(const char *s, log_level_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    for (int i = 0; i < LOG_LVL_COUNT; i++) {
        if (strcasecmp(s, level_names[i]) == 0) {
            *out = (log_level_t)i;
            return true;
        }
    }
    return false;
}

/* --- Task --- */

void task_log(void *arg)
{
    (void)arg;

    configASSERT(log_queue != NULL);    /* log_init() must run first */

    log_msg_t msg;
    uint32_t  reported = 0;

    for (;;) {
        (void)xQueueReceive(log_queue, &msg, portMAX_DELAY);

        if (msg.level == LOG_LVL_FLUSH) {
            sinks_flush();
            (void)xSemaphoreGive(flush_done);
            continue;
        }

        sinks_write((log_level_t)msg.level, msg.text, msg.len);

        /* Report drops once per change, as a WARN line of our own */
        uint32_t d = dropped;
        if (d != reported) {
            char line[48];
            int n = snprintf(line, sizeof(line), "[log] dropped %lu messages\r\n",
                             (unsigned long)(d - reported));
            if (n > 0) {
                sinks_write(LOG_LVL_WARN, line, ((size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1);
            }
            reported = d;
        }
    }
}
