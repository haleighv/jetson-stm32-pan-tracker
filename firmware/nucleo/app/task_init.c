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

/* Stack sizes in bytes. Uncomment rows as the tasks get written. */
static const task_descriptor_t task_list[] = {
    {task_cli,            "cli",          1024*3, NULL, TASK_PRIORITY_LOW,    NULL},
//    {task_host_comms, "host comms",       2048, NULL, TASK_PRIORITY_NORMAL, NULL},
    {task_log, "log",                     1024*2, NULL, TASK_PRIORITY_LOW,    NULL},
//    {task_supervisor,     "supervisor",   4096, NULL, TASK_PRIORITY_HIGH,   NULL},
//    {task_telemetry, "telemetry",         2048, NULL, TASK_PRIORITY_NORMAL, NULL},
};

static void init_tasks(void)
{
    /* Logging first, so every task can log from its first line */
    log_init();
    log_sink_uart_register();

    for (size_t i = 0; i < sizeof(task_list) / sizeof(task_list[0]); i++) {
        LOG_I("Initializing task %s", task_list[i].task_name);
        if (pdPASS != xTaskCreate(task_list[i].pfunc, task_list[i].task_name,
                                  task_list[i].stack_size / sizeof(StackType_t),
                                  task_list[i].param, task_list[i].priority, task_list[i].handle)) {
            LOG_E("%s creation failed", task_list[i].task_name);
        }
    }
}

void task_init_run(void *arg)
{
    (void)arg;
    init_tasks();        /* create the table's tasks */
    vTaskDelete(NULL);   /* init done; free this task */
}