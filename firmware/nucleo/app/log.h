/*
 * log.h
 *
 * Leveled logging. Callers format a message and queue it; the log task is
 * the only thing that touches the outputs ("sinks": UART now, flash and
 * UDP later). Callers never block on I/O.
 *
 * Notes:
 * - Task context only. Never call from an ISR (asserts).
 * - Never blocks. If the queue is full the message is dropped and counted.
 * - Each file sets its tag before including this header:
 *       #define LOG_TAG "cli"
 *       #include "log.h"
 * - Output format: [   12.345] I cli: message
 * - log_init() must run before any task logs. Before that, log_write()
 *   falls back to printf.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Config --- */

#define LOG_MSG_MAX         128     /* bytes per message incl. prefix and "\r\n", truncated past this */
#define LOG_QUEUE_DEPTH     32      /* messages; queue RAM = LOG_MSG_MAX * LOG_QUEUE_DEPTH */
#define LOG_MAX_SINKS       3       /* uart, flash, udp */

/* Messages above this level are compiled out entirely (e.g. set to
 * LOG_LVL_INFO for Release builds) */
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL   LOG_LVL_DEBUG
#endif

#ifndef LOG_TAG
#define LOG_TAG             "-"
#endif

/* --- Levels --- */

/* Lower number = more severe */
typedef enum {
    LOG_LVL_ERROR = 0,
    LOG_LVL_WARN,
    LOG_LVL_INFO,
    LOG_LVL_DEBUG,
    LOG_LVL_COUNT
} log_level_t;

/* --- Sinks --- */

/* An output for finished messages. Called only from the log task, one
 * message at a time, so a sink doesn't need its own locking. A slow sink
 * (flash) should buffer internally rather than stall the log task. */
typedef struct {
    const char  *name;                                  /* "uart", "flash", "udp" */
    bool       (*init)(void);                           /* optional, may be NULL; false = sink disabled */
    void       (*write)(const char *msg, size_t len);   /* msg is not NUL-terminated */
    void       (*flush)(void);                          /* optional, may be NULL */
    log_level_t  min_level;                             /* sink drops anything less severe */
    bool         enabled;
    bool         console;                               /* also gets log_console() text (CLI) */
} log_sink_t;

/* Register a sink. Call before or after log_init(). The struct must stay
 * valid forever (static). Returns false if LOG_MAX_SINKS is reached. */
bool log_sink_register(log_sink_t *sink);

/* Look up a registered sink by name, NULL if not found. For the CLI. */
log_sink_t *log_sink_find(const char *name);

/* --- Core --- */

/* Create the queue. Call once from init_tasks() before creating tasks. */
void log_init(void);

/* Global runtime filter, applied before formatting (cheap when filtered) */
void        log_set_level(log_level_t lvl);
log_level_t log_get_level(void);

/* Format and queue one message. Use the LOG_x macros instead. */
void log_write(log_level_t lvl, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Raw console text (CLI echo and command output). No prefix, not level
 * filtered, and only sent to sinks with console = true (uart, not flash).
 * Same queue as log_write(), so it never interleaves mid-line with logs.
 * Long text is split into LOG_MSG_MAX chunks. Never blocks; drops count. */
void log_console(const char *s, size_t len);

/* Messages dropped because the queue was full, since boot */
uint32_t log_dropped_count(void);

/* Ask every sink to flush (e.g. before a planned reset) */
void log_flush(void);

/* "ERROR", "WARN", ... and parse from string (case-insensitive).
 * log_level_from_str() returns false if the string isn't a level. */
const char *log_level_name(log_level_t lvl);
bool        log_level_from_str(const char *s, log_level_t *out);

/* --- Macros --- */

#define LOG_E(...) do { if (LOG_LVL_ERROR <= LOG_COMPILE_LEVEL) log_write(LOG_LVL_ERROR, LOG_TAG, __VA_ARGS__); } while (0)
#define LOG_W(...) do { if (LOG_LVL_WARN  <= LOG_COMPILE_LEVEL) log_write(LOG_LVL_WARN,  LOG_TAG, __VA_ARGS__); } while (0)
#define LOG_I(...) do { if (LOG_LVL_INFO  <= LOG_COMPILE_LEVEL) log_write(LOG_LVL_INFO,  LOG_TAG, __VA_ARGS__); } while (0)
#define LOG_D(...) do { if (LOG_LVL_DEBUG <= LOG_COMPILE_LEVEL) log_write(LOG_LVL_DEBUG, LOG_TAG, __VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
