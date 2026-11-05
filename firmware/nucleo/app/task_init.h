/*
 * task_init.h
 *
 * Task table types and the init task. The init task creates every task in
 * the table (task_init.c), waits for them at the sync point
 * (task_utilities.h), releases them, then deletes itself.
 *
 * Notes:
 * - Runs after the scheduler starts, so init code is allowed to block.
 * - Allocation is dynamic (heap_4) but only happens here, at boot.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef TASK_INIT_H
#define TASK_INIT_H

#include "FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TASK_PRIORITY_IDLE = 0,

    /* User Task priorities */
    TASK_PRIORITY_LOW,
    TASK_PRIORITY_NORMAL,
    TASK_PRIORITY_HIGH,
    TASK_PRIORITY_SUMMIT,   /* Highest priority level for user tasks */

    /* This level should never be used */
    TASK_PRIORITY_MAX = configMAX_PRIORITIES,
} task_priority_t;

typedef struct
{
    void (*pfunc) (void *pvParameters);
    const char *task_name;
    uint16_t stack_size;
    void *param;
    uint16_t priority;
    TaskHandle_t *handle;
    bool sync;              /* task calls task_sync() after its initialize() */
} task_descriptor_t;

void task_init_run(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* TASK_INIT_H */
