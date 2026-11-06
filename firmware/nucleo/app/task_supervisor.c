/*
 * task_supervisor.c
 *
 * Supervisor task. Owns the system state (BOOT/IDLE/ARMED/FAULT), starts
 * the control tick once every task is up, and reacts to error events on
 * the message bus. Runs every 10 ms.
 *
 * Notes:
 * - FAULT latches. It's only cleared on purpose (CLI clear_fault).
 * - TODO: clear_fault should arrive as a message, so only this task ever
 *   changes the state.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "supervisor"
#include "task_supervisor.h"
#include "log.h"
#include "msg.h"
#include "control.h"
#include "can.h"
#include "task_utilities.h"

#include "FreeRTOS.h"
#include "task.h"

#define CONTROL_WARMUP_MS   1000    /* jitter stats restart after this */

static TickType_t xLastStateChangeTime;   /* tick count when the state last changed */

static supervisor_state_t state = SUPERVISOR_BOOT;
static msg_queue_id_t supervisor_msg_queue_id;
static const char * const state_names[] = {
    [SUPERVISOR_BOOT] = "SUPERVISOR_BOOT",
    [SUPERVISOR_IDLE] = "SUPERVISOR_IDLE",
    [SUPERVISOR_ARMED] = "SUPERVISOR_ARMED",
    [SUPERVISOR_FAULT] = "SUPERVISOR_FAULT",
};

/* Private Functions */
static void supervisor_set_state(supervisor_state_t new_state)
{
    if (new_state >= SUPERVISOR_STATE_COUNT) {
        LOG_E("invalid state %d", (int)new_state);
        return;
    }
    LOG_I("state %s -> %s", state_names[state], state_names[new_state]);
    state = new_state;
    xLastStateChangeTime = xTaskGetTickCount();
}

/* Public Functions */
supervisor_state_t supervisor_get_state(void)
{
    return state;
}

uint32_t supervisor_get_state_ms(void)
{
    return xTaskGetTickCount() - xLastStateChangeTime;
}

bool supervisor_clear_fault(void)
{
    if (state == SUPERVISOR_FAULT) {
        LOG_I("clearing fault, returning to IDLE");
        supervisor_set_state(SUPERVISOR_IDLE);
        return true;
    }
    return false;
}

const char *supervisor_state_name(supervisor_state_t s)
{
    if (s >= SUPERVISOR_STATE_COUNT) {
        return "UNKNOWN";
    }
    return state_names[s];
}

/* --- Task --- */

static void initialize(void)
{
    event_id_t event_table[] = {
        EVENT_ERROR,
    };

    /* Subscribe to messages */
    supervisor_msg_queue_id = msg_subscriber_create("supervisor", 16);
    configASSERT(supervisor_msg_queue_id != MSG_QUEUE_ID_INVALID);
    for (size_t i = 0; i < sizeof(event_table) / sizeof(event_table[0]); i++) {
        configASSERT(msg_subscribe(supervisor_msg_queue_id, event_table[i]));
    }
}

/* Drain the queue without waiting; the 10 ms loop sets the pace */
static void handle_messages(void)
{
    msg_t message;

    while (msg_recv(supervisor_msg_queue_id, &message, 0)) {
        switch (message.id) {
        case EVENT_ERROR:
            LOG_E("error event (code %ld)", (long)message.data.i32[0]);
            if (state != SUPERVISOR_FAULT) {
                supervisor_set_state(SUPERVISOR_FAULT);
            }
            break;
        default:
            LOG_W("unexpected event %u", message.id);
            break;
        }
    }
}

void task_supervisor(void *arg)
{
    (void)arg;

    initialize();
    task_sync();

    /* Start the control tick and CAN only after every task is up */
    control_start();
    can_start();

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 10;

    /* Start the 10 ms schedule from here, after task_sync(), so it lines
     * up with when the loop actually starts */
    xLastWakeTime = xTaskGetTickCount();
    xLastStateChangeTime = xLastWakeTime;

    /* Boot (every task finishing init, log burst) delays a tick or two the
     * same way every time. Restart the jitter stats once that's over so
     * they show steady state. */
    const TickType_t control_started = xLastWakeTime;
    bool warmup_done = false;

    for (;;) {
        handle_messages();

        if (!warmup_done &&
            (xTaskGetTickCount() - control_started) >= pdMS_TO_TICKS(CONTROL_WARMUP_MS)) {
            control_reset_period();
            warmup_done = true;
            LOG_I("warm-up done, jitter stats restarted");
        }

    switch (state) {
        case SUPERVISOR_BOOT:
            supervisor_set_state(SUPERVISOR_IDLE);
            break;
        case SUPERVISOR_IDLE:
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            break;
        case SUPERVISOR_ARMED:
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            break;
        case SUPERVISOR_FAULT:
            vTaskDelayUntil(&xLastWakeTime, 1000);
            vTaskDelayUntil(&xLastWakeTime, 1000);
            break;
        default:
            LOG_E("unknown state %d", (int)state);
            state = SUPERVISOR_FAULT;
            break;
        }
    }
}