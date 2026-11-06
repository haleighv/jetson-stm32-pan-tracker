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
#include "version.h"
#include "control.h"
#include "msg.h"
#include "task_led.h"
#include "task_supervisor.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

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

/* Goes through the LED task like everything else, so the CLI and the
 * supervisor can't fight over a pin. */
static cli_status_t cmd_led(cli_t *cli, int argc, char **argv)
{
    led_mode_t mode;
    uint16_t   period = 0;

    if (argc < 3 || argc > 4 || argv[1][0] < '1' || argv[1][0] > '3' || argv[1][1] != '\0') {
        return CLI_ERR_ARGS;
    }
    if (!led_mode_from_str(argv[2], &mode)) {
        return CLI_ERR_ARGS;
    }
    if (argc == 4) {
        char *end;
        unsigned long ms = strtoul(argv[3], &end, 10);
        if (*end != '\0' || ms == 0 || ms > UINT16_MAX) {
            return CLI_ERR_ARGS;
        }
        period = (uint16_t)ms;
    }

    if (!led_set((led_id_t)(argv[1][0] - '1'), mode, period)) {
        cli_printf(cli, "message bus full, not sent\r\n");
    }
    return CLI_OK;
}

static cli_status_t cmd_msgs(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    msg_stats_t st;
    msg_get_stats(&st);

    cli_printf(cli, "posted %lu, bus full %lu, unrouted %lu\r\n",
               (unsigned long)st.posted, (unsigned long)st.post_dropped,
               (unsigned long)st.unrouted);
    for (msg_queue_id_t i = 0; i < (msg_queue_id_t)st.num_subs; i++) {
        cli_printf(cli, "  %-12s dropped %lu\r\n", msg_queue_name(i),
                   (unsigned long)msg_queue_dropped(i));
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

static cli_status_t cmd_state(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    supervisor_state_t s = supervisor_get_state();
    cli_printf(cli, "state %s for %lu ms\r\n", supervisor_state_name(s),
               (unsigned long)supervisor_get_state_ms());
    return CLI_OK;
}

static cli_status_t cmd_clear_fault(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (supervisor_clear_fault()) {
        cli_printf(cli, "fault cleared\r\n");
    } else {
        cli_printf(cli, "no fault to clear\r\n");
    }
    return CLI_OK;
}

/* Control tick timing. The ISR stores min/max period in CPU cycles; this
 * turns it into deviation from the ideal 1 ms, in ns. Integers only
 * (newlib-nano has no %f). "jitter clear" restarts the window. */
static cli_status_t cmd_jitter(cli_t *cli, int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        control_reset_period();
        cli_printf(cli, "jitter stats cleared\r\n");
        return CLI_OK;
    }
    if (argc != 1) {
        return CLI_ERR_ARGS;
    }

    uint32_t min_cyc, max_cyc;
    control_get_period(&min_cyc, &max_cyc);
    if (min_cyc == UINT32_MAX) {
        cli_printf(cli, "no samples yet\r\n");
        return CLI_OK;
    }

    uint32_t ideal = SystemCoreClock / 1000U;       /* cycles per 1 ms tick */
    uint32_t mhz   = SystemCoreClock / 1000000U;    /* cycles per us */

    /* Short periods come out negative */
    int32_t early_ns = (int32_t)((int64_t)(int32_t)(min_cyc - ideal) * 1000 / mhz);
    int32_t late_ns  = (int32_t)((int64_t)(int32_t)(max_cyc - ideal) * 1000 / mhz);

    cli_printf(cli, "period min %lu, max %lu cycles (ideal %lu)\r\n",
               (unsigned long)min_cyc, (unsigned long)max_cyc, (unsigned long)ideal);
    cli_printf(cli, "jitter %ld / +%ld ns, peak-to-peak %ld ns\r\n",
               (long)early_ns, (long)late_ns, (long)(late_ns - early_ns));

    /* When: ticks are ms since control_start(), about the same as uptime */
    uint32_t min_tick, max_tick;
    control_get_period_ticks(&min_tick, &max_tick);
    cli_printf(cli, "min at %lu.%03lu s, max at %lu.%03lu s\r\n",
               (unsigned long)(min_tick / 1000U), (unsigned long)(min_tick % 1000U),
               (unsigned long)(max_tick / 1000U), (unsigned long)(max_tick % 1000U));
    return CLI_OK;
}

const cli_cmd_t cli_cmds[] = {
    /* Any order works ("?" sorts its output); kept alphabetical to read easily */
    { "clear_fault", "clear_fault",                        "clear a latched fault",              cmd_clear_fault },
    { "heap",        "heap",                               "FreeRTOS heap use",                  cmd_heap        },
    { "jitter",      "jitter [clear]",                     "control tick timing (DWT)",          cmd_jitter      },
    { "led",         "led <1-3> <on|off|blink|fade> [ms]", "LED mode (1 green, 2 amber, 3 red)", cmd_led         },
    { "log_level",   "log_level [error|warn|info|debug]",  "show or set the log level",          cmd_loglevel    },
    { "msgs",        "msgs",                               "message bus counters",               cmd_msgs        },
    { "reset",       "reset",                              "software reset",                     cmd_reset       },
    { "state",       "state",                              "supervisor state and time in it",    cmd_state       },
    { "tasks",       "tasks",                              "task state, priority, stack free",   cmd_tasks       },
    { "uptime",      "uptime",                             "time since boot",                    cmd_uptime      },
    { "version",     "version",                            "git hash, build type, build time",   cmd_version     },
};

const size_t cli_cmds_count = sizeof(cli_cmds) / sizeof(cli_cmds[0]);
