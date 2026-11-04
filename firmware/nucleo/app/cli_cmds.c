/*
 * cli_cmds.c
 *
 * Board commands. Each handler prints with cli_printf() and returns
 * CLI_OK, or CLI_ERR_ARGS to have the CLI print its usage line.
 * Add a command: write a cmd_x() and add a row to cli_cmds[].
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "cli_cmds.h"
#include "log.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "version.h"

#define TASKS_MAX   16      /* rows "tasks" can show */

static cli_status_t cmd_version(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
      cli_printf(cli, "%s (%s) %s, built %s %s\r\n",
             FW_VERSION_STR, FW_GIT_DESCRIBE, FW_BUILD_TYPE, __DATE__, __TIME__);
    return CLI_OK;
}

static cli_status_t cmd_uptime(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    cli_printf(cli, "%lu.%03lu s\r\n", (unsigned long)(ms / 1000U), (unsigned long)(ms % 1000U));
    return CLI_OK;
}

static char state_char(eTaskState s)
{
    switch (s) {
    case eRunning:   return 'X';
    case eReady:     return 'R';
    case eBlocked:   return 'B';
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

/* uxTaskGetSystemState() needs configUSE_TRACE_FACILITY = 1 */
static cli_status_t cmd_tasks(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static TaskStatus_t st[TASKS_MAX];      /* static: ~36 B each, keep it off the stack */

    UBaseType_t n = uxTaskGetSystemState(st, TASKS_MAX, NULL);
    if (n == 0) {
        cli_printf(cli, "more than %d tasks\r\n", TASKS_MAX);
        return CLI_OK;
    }

    cli_printf(cli, "%-16s st prio stack_free(words)\r\n", "name");
    for (UBaseType_t i = 0; i < n; i++) {
        cli_printf(cli, "%-16s %c  %4lu %6lu\r\n",
                   st[i].pcTaskName, state_char(st[i].eCurrentState),
                   (unsigned long)st[i].uxCurrentPriority,
                   (unsigned long)st[i].usStackHighWaterMark);
    }
    cli_printf(cli, "st: X running, R ready, B blocked, S suspended, D deleted\r\n");
    return CLI_OK;
}

static cli_status_t cmd_heap(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cli_printf(cli, "free %lu, min ever %lu, total %lu bytes\r\n",
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize(),
               (unsigned long)configTOTAL_HEAP_SIZE);
    return CLI_OK;
}

static cli_status_t cmd_led(cli_t *cli, int argc, char **argv)
{
    (void)cli;
    static GPIO_TypeDef *const port[] = { LD1_GPIO_Port, LD2_GPIO_Port, LD3_GPIO_Port };
    static const uint16_t      pin[]  = { LD1_Pin,       LD2_Pin,       LD3_Pin };

    if (argc != 3 || argv[1][0] < '1' || argv[1][0] > '3' || argv[1][1] != '\0') {
        return CLI_ERR_ARGS;
    }
    int i = argv[1][0] - '1';

    if (strcmp(argv[2], "on") == 0) {
        HAL_GPIO_WritePin(port[i], pin[i], GPIO_PIN_SET);
    } else if (strcmp(argv[2], "off") == 0) {
        HAL_GPIO_WritePin(port[i], pin[i], GPIO_PIN_RESET);
    } else if (strcmp(argv[2], "toggle") == 0) {
        HAL_GPIO_TogglePin(port[i], pin[i]);
    } else {
        return CLI_ERR_ARGS;
    }
    return CLI_OK;
}

static cli_status_t cmd_loglevel(cli_t *cli, int argc, char **argv)
{
    if (argc == 2) {
        log_level_t lvl;
        if (!log_level_from_str(argv[1], &lvl)) {
            return CLI_ERR_ARGS;
        }
        log_set_level(lvl);
    } else if (argc != 1) {
        return CLI_ERR_ARGS;
    }
    cli_printf(cli, "level %s, dropped %lu\r\n",
               log_level_name(log_get_level()), (unsigned long)log_dropped_count());
    return CLI_OK;
}

static cli_status_t cmd_reset(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cli_printf(cli, "resetting\r\n");
    log_flush();            /* let the line above go out first */
    NVIC_SystemReset();
    return CLI_OK;          /* not reached */
}

const cli_cmd_t cli_cmds[] = {
    /* Any order works ("?" sorts its output); kept alphabetical to read easily */
    { "heap",     "heap",                             "FreeRTOS heap use",                 cmd_heap },
    { "led",      "led <1-3> <on|off|toggle>",        "drive a user LED",                  cmd_led },
    { "loglevel", "loglevel [error|warn|info|debug]", "show or set the log level",         cmd_loglevel },
    { "reset",    "reset",                            "software reset",                    cmd_reset },
    { "tasks",    "tasks",                            "task state, priority, stack free",  cmd_tasks },
    { "uptime",   "uptime",                           "time since boot",                   cmd_uptime },
    { "version",  "version",                          "git hash, build type, build time",  cmd_version },
};

const size_t cli_cmds_count = sizeof(cli_cmds) / sizeof(cli_cmds[0]);
