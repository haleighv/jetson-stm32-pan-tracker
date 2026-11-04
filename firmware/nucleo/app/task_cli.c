/*
 * task_cli.c
 *
 * CLI task. Placeholder for now: logs once a second so the log path
 * can be checked. UART RX + command parsing comes next.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "cli"
#include "task_cli.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

void task_cli(void *arg)
{
    (void)arg;

    for (;;) {
        LOG_I("alive");
        LOG_D("debug detail");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
