/*
 * cli.c
 *
 * Command line core. See cli.h.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define KEY_CTRL_C  0x03
#define KEY_BS      0x08
#define KEY_ESC     0x1b
#define KEY_DEL     0x7f

/* Escape sequence states: ESC, then '[' (CSI), then a final byte 0x40-0x7e */
enum { ESC_NONE = 0, ESC_START, ESC_CSI };

static void out(cli_t *cli, const char *s)
{
    cli->write(s, strlen(s));
}

void cli_init(cli_t *cli, const cli_cmd_t *cmds, size_t num_cmds,
              cli_write_fn_t write, const char *prompt)
{
    memset(cli, 0, sizeof(*cli));
    cli->cmds = cmds;
    cli->num_cmds = num_cmds;
    cli->write = write;
    cli->prompt = prompt;
    cli->hist_pos = -1;
}

void cli_prompt(cli_t *cli)
{
    if (cli->prompt != NULL) {
        out(cli, cli->prompt);
    }
}

void cli_printf(cli_t *cli, const char *fmt, ...)
{
    char buf[CLI_PRINTF_MAX];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        size_t len = ((size_t)n < sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
        cli->write(buf, len);
    }
}

static const cli_cmd_t *find_cmd(const cli_t *cli, const char *name)
{
    for (size_t i = 0; i < cli->num_cmds; i++) {
        if (strcmp(cli->cmds[i].name, name) == 0) {
            return &cli->cmds[i];
        }
    }
    return NULL;
}

#define HELP_NAME   "?"
#define HELP_TEXT   "list commands, or '? <cmd>'"

/* Built in, so every table gets it for free. Prints alphabetically
 * without sorting the table: each pass prints the smallest name that's
 * bigger than the last one printed. O(n^2), fine for a handful of commands. */
static cli_status_t cmd_help(cli_t *cli, int argc, char **argv)
{
    if (argc >= 2) {
        const cli_cmd_t *cmd = find_cmd(cli, argv[1]);
        if (cmd == NULL) {
            cli_printf(cli, "unknown command: %s\r\n", argv[1]);
            return CLI_ERR_UNKNOWN;
        }
        cli_printf(cli, "usage: %s\r\n  %s\r\n", cmd->usage, cmd->help);
        return CLI_OK;
    }

    const char *prev = NULL;

    for (size_t pass = 0; pass <= cli->num_cmds; pass++) {
        const char *name = NULL;
        const char *help = NULL;

        /* "?" takes part in the ordering like any other command */
        if (prev == NULL || strcmp(HELP_NAME, prev) > 0) {
            name = HELP_NAME;
            help = HELP_TEXT;
        }
        for (size_t i = 0; i < cli->num_cmds; i++) {
            const char *n = cli->cmds[i].name;
            if ((prev == NULL || strcmp(n, prev) > 0) &&
                (name == NULL || strcmp(n, name) < 0)) {
                name = n;
                help = cli->cmds[i].help;
            }
        }
        if (name == NULL) {
            break;
        }
        cli_printf(cli, "  %-10s %s\r\n", name, help);
        prev = name;
    }
    return CLI_OK;
}

cli_status_t cli_execute(cli_t *cli, char *line)
{
    char *argv[CLI_ARGV_MAX];
    int argc = 0;
    char *p = line;

    /* Split on spaces/tabs in place */
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        if (argc == CLI_ARGV_MAX) {
            out(cli, "too many arguments\r\n");
            return CLI_ERR_TOO_MANY;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    if (argc == 0) {
        return CLI_EMPTY;
    }

    if (strcmp(argv[0], HELP_NAME) == 0) {
        return cmd_help(cli, argc, argv);
    }

    const cli_cmd_t *cmd = find_cmd(cli, argv[0]);
    if (cmd == NULL) {
        cli_printf(cli, "unknown command: %s (try '?')\r\n", argv[0]);
        return CLI_ERR_UNKNOWN;
    }

    cli_status_t rc = cmd->fn(cli, argc, argv);
    if (rc == CLI_ERR_ARGS) {
        cli_printf(cli, "usage: %s\r\n", cmd->usage);
    }
    return rc;
}

/* --- History --- */

/* Save a finished line as the newest entry. Skips blanks and repeats. */
static void history_add(cli_t *cli, const char *line)
{
    if (line[strspn(line, " \t")] == '\0') {
        return;     /* blank or only spaces */
    }
    if (cli->hist_count > 0 && strcmp(cli->history[0], line) == 0) {
        return;
    }
    for (int i = CLI_HISTORY_DEPTH - 1; i > 0; i--) {
        memcpy(cli->history[i], cli->history[i - 1], sizeof(cli->history[i]));
    }
    strncpy(cli->history[0], line, CLI_LINE_MAX);
    cli->history[0][CLI_LINE_MAX] = '\0';
    if (cli->hist_count < CLI_HISTORY_DEPTH) {
        cli->hist_count++;
    }
}

/* Replace the line being edited and redraw it. One write: return to
 * column 0, clear to end of line (ANSI ESC [K), prompt, line. */
static void line_replace(cli_t *cli, const char *text)
{
    strncpy(cli->line, text, CLI_LINE_MAX);
    cli->line[CLI_LINE_MAX] = '\0';
    cli->len = strlen(cli->line);
    cli_printf(cli, "\r\x1b[K%s%s", (cli->prompt != NULL) ? cli->prompt : "", cli->line);
}

static void history_up(cli_t *cli)
{
    if (cli->hist_pos + 1 >= cli->hist_count) {
        return;     /* already at the oldest */
    }
    if (cli->hist_pos < 0) {
        cli->line[cli->len] = '\0';
        memcpy(cli->saved, cli->line, sizeof(cli->saved));
    }
    cli->hist_pos++;
    line_replace(cli, cli->history[cli->hist_pos]);
}

static void history_down(cli_t *cli)
{
    if (cli->hist_pos < 0) {
        return;     /* not browsing */
    }
    cli->hist_pos--;
    line_replace(cli, (cli->hist_pos < 0) ? cli->saved : cli->history[cli->hist_pos]);
}

/* --- Input --- */

void cli_input(cli_t *cli, char c)
{
    /* Escape sequences: up/down arrows (ESC [ A / ESC [ B) browse history,
     * everything else is swallowed */
    if (cli->esc == ESC_START) {
        cli->esc = (c == '[') ? ESC_CSI : ESC_NONE;
        return;
    }
    if (cli->esc == ESC_CSI) {
        if (c >= 0x40 && c <= 0x7e) {
            cli->esc = ESC_NONE;
            if (c == 'A') {
                history_up(cli);
            } else if (c == 'B') {
                history_down(cli);
            }
        }
        return;
    }

    /* LF right after CR is the second half of CRLF */
    if (c == '\n' && cli->last_cr) {
        cli->last_cr = false;
        return;
    }
    cli->last_cr = (c == '\r');

    switch (c) {
    case '\r':
    case '\n':
        out(cli, "\r\n");
        cli->line[cli->len] = '\0';
        history_add(cli, cli->line);        /* before execute splits the line */
        (void)cli_execute(cli, cli->line);
        cli->len = 0;
        cli->hist_pos = -1;
        cli_prompt(cli);
        break;

    case KEY_BS:
    case KEY_DEL:
        if (cli->len > 0) {
            cli->len--;
            out(cli, "\b \b");
        }
        break;

    case KEY_CTRL_C:
        cli->len = 0;
        cli->hist_pos = -1;
        out(cli, "^C\r\n");
        cli_prompt(cli);
        break;

    case KEY_ESC:
        cli->esc = ESC_START;
        break;

    default:
        /* Printable only; the rest of the line is ignored once full */
        if (c >= 0x20 && c < 0x7f && cli->len < CLI_LINE_MAX) {
            cli->line[cli->len++] = c;
            cli->write(&c, 1);
        }
        break;
    }
}
