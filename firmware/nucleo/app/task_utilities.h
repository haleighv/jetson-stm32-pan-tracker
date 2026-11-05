/*
 * task_utilities.h
 *
 * Helpers shared by all tasks. For now: the startup sync point.
 *
 * Startup sync:
 * - Each task runs its own initialize() (create timers, subscribe to
 *   events, ...), then calls task_sync() and blocks there.
 * - The init task creates every task, then calls task_sync_unblock(),
 *   which waits until every task has reached task_sync(). Then it opens
 *   the gate and all tasks start their main loops, highest priority first.
 * - Result: no task's loop runs (or posts a message) until every task has
 *   finished initializing and subscribing, so nothing is lost at boot.
 *
 * Notes:
 * - Every task counted in task_sync_unblock() must call task_sync(), or
 *   the init task waits forever (it logs a warning every second).
 * - The log task does not sync: it has to run during init to print what
 *   the others are doing.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef TASK_UTILITIES_H
#define TASK_UTILITIES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TASK_SYNC_MAX   16      /* most tasks that can sync */

/* Create the sync semaphores. Init task only, before creating tasks. */
void task_sync_init(void);

/* Called by each task after its initialize(). Blocks until released. */
void task_sync(void);

/* Init task only: wait until num_tasks tasks have called task_sync(),
 * then release them all. */
void task_sync_unblock(uint8_t num_tasks);

#ifdef __cplusplus
}
#endif

#endif /* TASK_UTILITIES_H */
