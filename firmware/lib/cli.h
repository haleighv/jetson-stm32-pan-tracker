/*
 * cli.h
 *
 * Small command line: line editing, argv splitting, and dispatch through a
 * command table. Portable (no HAL, no RTOS) so it's built on the host for
 * unit tests. The board side feeds it bytes and gives it a write function.
 *
 * Notes:
 * - Feed every received byte to cli_input(). Enter (CR, LF, or CRLF) runs
 *   the line.
 * - Editing: backspace/delete, Ctrl-C clears the line. Up/down arrows step
 *   through the last CLI_HISTORY_DEPTH lines. Other escape sequences are
 *   ignored. No quoting: args split on spaces/tabs.
 * - "?" is built in and lists commands alphabetically. "? <cmd>" shows
 *   that command's usage.
 * - Not thread-safe. One cli_t per input stream, used from one task.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_LINE_MAX    80      /* chars per line, extra input is ignored */
#define CLI_ARGV_MAX    8       /* args per line, including the command */
#define CLI_PRINTF_MAX  128     /* longest single cli_printf() output */
#define CLI_HISTORY_DEPTH 2     /* lines kept for up/down arrows */

/* Command return codes. Commands return CLI_OK or CLI_ERR_ARGS. */
typedef enum {
    CLI_OK          =  0,
    CLI_ERR_ARGS    = -1,       /* bad args: cli prints the usage line */
    CLI_ERR_UNKNOWN = -2,       /* no such command */
    CLI_ERR_TOO_MANY = -3,      /* more than CLI_ARGV_MAX args */
    CLI_EMPTY       = -4        /* blank line, nothing run */
} cli_status_t;

typedef struct cli cli_t;

typedef cli_status_t (*cli_cmd_fn_t)(cli_t *cli, int argc, char **argv);
typedef void (*cli_write_fn_t)(const char *s, size_t len);

typedef struct {
    const char   *name;         /* what you type */
    const char   *usage;        /* e.g. "led <1-3> <on|off|toggle>" */
    const char   *help;         /* one line for "?" */
    cli_cmd_fn_t  fn;
} cli_cmd_t;

/* State, set up by cli_init(). Fields are private. */
struct cli {
    const cli_cmd_t *cmds;
    size_t           num_cmds;
    cli_write_fn_t   write;
    const char      *prompt;
    char             line[CLI_LINE_MAX + 1];
    size_t           len;
    uint8_t          esc;       /* escape sequence state */
    bool             last_cr;   /* swallow the LF of a CRLF */

    /* History: [0] is the newest. hist_pos -1 means editing a new line;
     * saved holds that line while browsing so down-arrow can return to it. */
    char             history[CLI_HISTORY_DEPTH][CLI_LINE_MAX + 1];
    uint8_t          hist_count;
    int8_t           hist_pos;
    char             saved[CLI_LINE_MAX + 1];
};

/* cmds must stay valid (static). prompt may be NULL for none. */
void cli_init(cli_t *cli, const cli_cmd_t *cmds, size_t num_cmds,
              cli_write_fn_t write, const char *prompt);

/* Feed one received byte. Echoes, edits, and runs the line on Enter. */
void cli_input(cli_t *cli, char c);

/* Split and run one line. line is modified (split in place). Used by
 * cli_input(); also handy for tests and for running commands from code. */
cli_status_t cli_execute(cli_t *cli, char *line);

/* Print the prompt */
void cli_prompt(cli_t *cli);

/* Output for command handlers. Truncated at CLI_PRINTF_MAX. */
void cli_printf(cli_t *cli, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

#endif /* CLI_H */
