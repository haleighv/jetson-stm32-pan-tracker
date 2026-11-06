/*
 * task_utilities.c
 *
 * Startup sync point, see task_utilities.h.
 *
 * Two semaphores:
 * - ready: counting. Each task gives it once when its init is done. A
 *   counting semaphore so gives aren't lost if several tasks finish before
 *   the init task gets around to taking.
 * - gate:  binary, starts empty. The init task gives it once when everyone
 *   is ready. The first waiter (highest priority) takes it, then gives it
 *   back for the next waiter, so the release ripples through every task.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "sync"
#include "task_utilities.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define SYNC_WARN_MS    1000    /* init task logs who it's still waiting on this often */

static SemaphoreHandle_t ready_sem;
static SemaphoreHandle_t gate_sem;

void task_sync_init(void)
{
    ready_sem = xSemaphoreCreateCounting(TASK_SYNC_MAX, 0);
    gate_sem  = xSemaphoreCreateBinary();
    configASSERT(ready_sem != NULL);
    configASSERT(gate_sem != NULL);
}

void task_sync(void)
{
    if (ready_sem == NULL || gate_sem == NULL) {
        LOG_E("task_sync_init() not called");
        return;
    }

    const char *name = pcTaskGetName(NULL);

    /* Log before giving: the init task runs at a higher priority and wakes
     * as soon as the last task gives, so logging after would print out of
     * order */
    LOG_D("%s waiting at sync point", name);
    (void)xSemaphoreGive(ready_sem);

    /* Wait for the gate, then pass it on to the next waiting task */
    (void)xSemaphoreTake(gate_sem, portMAX_DELAY);
    (void)xSemaphoreGive(gate_sem);
    LOG_D("%s released", name);
}

void task_sync_unblock(uint8_t num_tasks)
{
    if (ready_sem == NULL || gate_sem == NULL) {
        LOG_E("task_sync_init() not called");
        return;
    }

    uint8_t ready = 0;
    while (ready < num_tasks) {
        if (xSemaphoreTake(ready_sem, pdMS_TO_TICKS(SYNC_WARN_MS)) == pdPASS) {
            ready++;
        } else {
            /* Keep waiting: releasing early would let tasks run before
             * everyone has subscribed. A hung init shows up here. */
            LOG_W("still waiting on %u of %u tasks", num_tasks - ready, num_tasks);
        }
    }

    LOG_I("all %u tasks initialized, starting", num_tasks);
    (void)xSemaphoreGive(gate_sem);
}
