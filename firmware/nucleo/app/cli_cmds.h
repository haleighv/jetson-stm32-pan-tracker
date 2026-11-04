/*
 * cli_cmds.h
 *
 * Board command table for the CLI (version, uptime, tasks, heap, led,
 * loglevel, reset). The parser itself is portable, in lib/cli.c.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef CLI_CMDS_H
#define CLI_CMDS_H

#include <stddef.h>
#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const cli_cmd_t cli_cmds[];
extern const size_t    cli_cmds_count;

#ifdef __cplusplus
}
#endif

#endif /* CLI_CMDS_H */
