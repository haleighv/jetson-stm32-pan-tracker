/*
 * test_cli.c
 *
 * Host unit tests for cli.c (Unity). Output goes to a capture buffer.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "unity.h"
#include "cli.h"

/* --- Capture output --- */

static char   out_buf[1024];
static size_t out_len;

static void capture(const char *s, size_t len)
{
    if (out_len + len < sizeof(out_buf)) {
        memcpy(out_buf + out_len, s, len);
        out_len += len;
        out_buf[out_len] = '\0';
    }
}

/* --- A test command that records what it got --- */

static int   last_argc;
static char  last_argv[CLI_ARGV_MAX][CLI_LINE_MAX + 1];
static int   calls;

static cli_status_t cmd_echo(cli_t *cli, int argc, char **argv)
{
    (void)cli;
    calls++;
    last_argc = argc;
    for (int i = 0; i < argc; i++) {
        strcpy(last_argv[i], argv[i]);
    }
    return CLI_OK;
}

static cli_status_t cmd_needs_arg(cli_t *cli, int argc, char **argv)
{
    (void)cli;
    (void)argv;
    return (argc == 2) ? CLI_OK : CLI_ERR_ARGS;
}

/* Deliberately not in alphabetical order */
static const cli_cmd_t cmds[] = {
    { "echo",  "echo [args...]", "record args",     cmd_echo },
    { "need",  "need <x>",       "needs one arg",   cmd_needs_arg },
    { "alpha", "alpha",          "same as echo",    cmd_echo },
};

static cli_t cli;

static void type(const char *s)
{
    while (*s != '\0') {
        cli_input(&cli, *s++);
    }
}

void setUp(void)
{
    out_len = 0;
    out_buf[0] = '\0';
    calls = 0;
    last_argc = 0;
    cli_init(&cli, cmds, sizeof(cmds) / sizeof(cmds[0]), capture, "> ");
}

void tearDown(void) {}

/* --- Tests --- */

/* Typed line is split on spaces and the command gets argc/argv */
static void test_dispatch_with_args(void)
{
    type("echo  a bb   ccc\r");

    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_INT(4, last_argc);
    TEST_ASSERT_EQUAL_STRING("echo", last_argv[0]);
    TEST_ASSERT_EQUAL_STRING("a", last_argv[1]);
    TEST_ASSERT_EQUAL_STRING("bb", last_argv[2]);
    TEST_ASSERT_EQUAL_STRING("ccc", last_argv[3]);
}

/* CRLF runs the line once, not twice */
static void test_crlf_runs_once(void)
{
    type("echo\r\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
}

/* Backspace removes the last char before the line runs */
static void test_backspace_edits_line(void)
{
    type("echx\b" "o hi\r");

    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("echo", last_argv[0]);
    TEST_ASSERT_EQUAL_STRING("hi", last_argv[1]);
}

/* Arrow keys (ESC [ A) are ignored, not added to the line */
static void test_escape_sequence_ignored(void)
{
    type("ec\x1b[Aho\r");

    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("echo", last_argv[0]);
}

/* Ctrl-C drops the line without running it */
static void test_ctrl_c_clears_line(void)
{
    type("echo\x03");
    TEST_ASSERT_EQUAL_INT(0, calls);

    type("echo\r");
    TEST_ASSERT_EQUAL_INT(1, calls);
}

/* Tabs also separate args (cli_execute; the line editor drops typed tabs) */
static void test_tabs_split_args(void)
{
    char line[] = "echo\ta\t\tb";

    TEST_ASSERT_EQUAL_INT(CLI_OK, cli_execute(&cli, line));
    TEST_ASSERT_EQUAL_INT(3, last_argc);
    TEST_ASSERT_EQUAL_STRING("b", last_argv[2]);
}

static void test_unknown_command(void)
{
    char line[] = "nope";

    TEST_ASSERT_EQUAL_INT(CLI_ERR_UNKNOWN, cli_execute(&cli, line));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "unknown command: nope"));
}

/* CLI_ERR_ARGS from a command prints its usage line */
static void test_bad_args_prints_usage(void)
{
    char line[] = "need";

    TEST_ASSERT_EQUAL_INT(CLI_ERR_ARGS, cli_execute(&cli, line));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "usage: need <x>"));
}

static void test_too_many_args(void)
{
    char line[] = "echo 1 2 3 4 5 6 7 8";      /* 9 args, max is 8 */

    TEST_ASSERT_EQUAL_INT(CLI_ERR_TOO_MANY, cli_execute(&cli, line));
    TEST_ASSERT_EQUAL_INT(0, calls);
}

static void test_blank_line_is_empty(void)
{
    char line[] = "   \t ";
    TEST_ASSERT_EQUAL_INT(CLI_EMPTY, cli_execute(&cli, line));
}

/* "?" lists every command plus itself, alphabetically */
static void test_help_lists_commands_sorted(void)
{
    char line[] = "?";

    TEST_ASSERT_EQUAL_INT(CLI_OK, cli_execute(&cli, line));

    const char *q     = strstr(out_buf, "  ? ");
    const char *alpha = strstr(out_buf, "  alpha ");
    const char *echo  = strstr(out_buf, "  echo ");
    const char *need  = strstr(out_buf, "  need ");
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_NOT_NULL(alpha);
    TEST_ASSERT_NOT_NULL(echo);
    TEST_ASSERT_NOT_NULL(need);
    TEST_ASSERT_TRUE(q < alpha);
    TEST_ASSERT_TRUE(alpha < echo);
    TEST_ASSERT_TRUE(echo < need);
}

/* "? <cmd>" prints that command's usage */
static void test_help_one_command(void)
{
    char line[] = "? need";

    TEST_ASSERT_EQUAL_INT(CLI_OK, cli_execute(&cli, line));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "usage: need <x>"));
}

/* --- History --- */

#define UP      "\x1b[A"
#define DOWN    "\x1b[B"

/* Up recalls the last line; Enter runs it again */
static void test_history_up_recalls_last(void)
{
    type("echo one\r");
    type(UP "\r");

    TEST_ASSERT_EQUAL_INT(2, calls);
    TEST_ASSERT_EQUAL_STRING("one", last_argv[1]);
}

/* Up twice gets the older line; a third up stays on the oldest (depth 2) */
static void test_history_depth_two(void)
{
    type("echo one\r");
    type("echo two\r");
    type("echo three\r");
    type(UP UP UP "\r");

    TEST_ASSERT_EQUAL_STRING("two", last_argv[1]);
}

/* Down after up returns to what was being typed */
static void test_history_down_restores_typing(void)
{
    type("echo one\r");
    type("echo par");
    type(UP DOWN "tial\r");

    TEST_ASSERT_EQUAL_STRING("partial", last_argv[1]);
}

/* Blank lines and immediate repeats aren't stored */
static void test_history_skips_blank_and_repeat(void)
{
    type("echo one\r");
    type("echo two\r");
    type("echo two\r");
    type("   \r");
    type(UP UP "\r");

    TEST_ASSERT_EQUAL_STRING("one", last_argv[1]);
}

/* Input past CLI_LINE_MAX is dropped, no overflow */
static void test_long_line_truncated(void)
{
    type("echo ");
    for (int i = 0; i < CLI_LINE_MAX + 20; i++) {
        cli_input(&cli, 'x');
    }
    cli_input(&cli, '\r');

    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_size_t(CLI_LINE_MAX - 5, strlen(last_argv[1]));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_with_args);
    RUN_TEST(test_crlf_runs_once);
    RUN_TEST(test_backspace_edits_line);
    RUN_TEST(test_escape_sequence_ignored);
    RUN_TEST(test_ctrl_c_clears_line);
    RUN_TEST(test_tabs_split_args);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_bad_args_prints_usage);
    RUN_TEST(test_too_many_args);
    RUN_TEST(test_blank_line_is_empty);
    RUN_TEST(test_help_lists_commands_sorted);
    RUN_TEST(test_help_one_command);
    RUN_TEST(test_history_up_recalls_last);
    RUN_TEST(test_history_depth_two);
    RUN_TEST(test_history_down_restores_typing);
    RUN_TEST(test_history_skips_blank_and_repeat);
    RUN_TEST(test_long_line_truncated);
    return UNITY_END();
}
