/*
 * task_supervisor.h
 *
 * Supervisor task: system state machine and the getters the CLI uses.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef TASK_SUPERVISOR_H
#define TASK_SUPERVISOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SUPERVISOR_BOOT = 0,
    SUPERVISOR_IDLE,
    SUPERVISOR_ARMED,
    SUPERVISOR_FAULT,
    SUPERVISOR_STATE_COUNT
} supervisor_state_t;

void task_supervisor(void *arg);

/* Getters: safe from any task (single-word reads) */
supervisor_state_t supervisor_get_state(void);
uint32_t supervisor_get_state_ms(void);
const char *supervisor_state_name(supervisor_state_t s);

/* FAULT -> IDLE. Returns false if not in FAULT.
 * TODO: changes state from the caller's task; move to a message. */
bool supervisor_clear_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_SUPERVISOR_H */
