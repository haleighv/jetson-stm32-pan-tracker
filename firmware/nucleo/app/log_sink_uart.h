/*
 * log_sink_uart.h
 *
 * Log sink that writes to USART3 (ST-LINK virtual COM port on the Nucleo).
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef LOG_SINK_UART_H
#define LOG_SINK_UART_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the uart sink with the logger. Call from init_tasks(). */
bool log_sink_uart_register(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_SINK_UART_H */
