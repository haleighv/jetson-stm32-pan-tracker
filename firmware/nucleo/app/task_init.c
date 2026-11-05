/*
 * task_init.c
 *
 * Task table and init. Add a task by adding a row to task_list[].
 * Logging is set up first so every task can log from its first line.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "init"
#include "task_init.h"
#include "log.h"
#include "log_sink_uart.h"
#include "task_cli.h"
#include "task_log.h"
#include "task_supervisor.h"
#include "task_led.h"
#include "msg.h"
#include "task_utilities.h"

/* Stack sizes in bytes. Uncomment rows as the tasks get written.
 * sync: true if the task calls task_sync(). log is false so it can print
 * while everyone else initializes. */
static const task_descriptor_t task_list[] = {
    {task_cli,            "cli",          1024*3, NULL, TASK_PRIORITY_LOW,    NULL, true },
//    {task_host_comms, "host comms",       2048, NULL, TASK_PRIORITY_NORMAL, NULL, true },
    {task_log, "log",                     1024*2, NULL, TASK_PRIORITY_LOW,    NULL, false},
    {task_led,            "led",          1024*2, NULL, TASK_PRIORITY_LOW,    NULL, true },
    {task_msg,            "msg",          1024*1, NULL, TASK_PRIORITY_SUMMIT, NULL, true },   /* dispatcher */
    {task_supervisor,     "supervisor",   1024*3, NULL, TASK_PRIORITY_HIGH,   NULL, true },
//    {task_telemetry, "telemetry",         2048, NULL, TASK_PRIORITY_NORMAL, NULL, true },
};

/* Returns how many created tasks will call task_sync() */
static uint8_t init_tasks(void)
{
    uint8_t num_sync = 0;

    /* Logging first, so every task can log from its first line */
    log_init();
    log_sink_uart_register();

    /* Message bus before tasks, so any task can post from its first line */
    msg_init();

    /* Sync point before tasks, so the first one to finish can check in */
    task_sync_init();

    for (size_t i = 0; i < sizeof(task_list) / sizeof(task_list[0]); i++) {
        LOG_I("Initializing task %s", task_list[i].task_name);
        if (pdPASS != xTaskCreate(task_list[i].pfunc, task_list[i].task_name,
                                  task_list[i].stack_size / sizeof(StackType_t),
                                  task_list[i].param, task_list[i].priority, task_list[i].handle)) {
            LOG_E("%s creation failed", task_list[i].task_name);
        } else if (task_list[i].sync) {
            num_sync++;     /* only count tasks that exist, or unblock waits forever */
        }
    }
    return num_sync;
}

void task_init_run(void *arg)
{
    (void)arg;
    uint8_t num_sync = init_tasks();    /* create the table's tasks */
    task_sync_unblock(num_sync);        /* wait for their init, then release */
    vTaskDelete(NULL);                  /* init done; free this task */
}