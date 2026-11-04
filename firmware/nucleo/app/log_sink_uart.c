/*
 * log_sink_uart.c
 *
 * Log sink for USART3. Blocking transmit is fine here: only the log task
 * calls write(), and it's the lowest priority task.
 *
 * Note: printf (retarget.c) also uses huart3. Once logging is up, only use
 * printf before the scheduler starts or in fault handlers, otherwise the
 * two can collide (HAL returns BUSY and the line is lost).
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "log_sink_uart.h"
#include "log.h"
#include "usart.h"

/* 128 bytes at 115200 baud is ~11 ms. Generous bound so a wedged UART
 * can't hang the log task forever. */
#define UART_SINK_TIMEOUT_MS    50

static void uart_write(const char *msg, size_t len)
{
    (void)HAL_UART_Transmit(&huart3, (const uint8_t *)msg, (uint16_t)len, UART_SINK_TIMEOUT_MS);
}

static log_sink_t uart_sink = {
    .name      = "uart",
    .init      = NULL,              /* USART3 is set up by MX_USART3_UART_Init() */
    .write     = uart_write,
    .flush     = NULL,              /* blocking transmit, nothing buffered */
    .min_level = LOG_LVL_DEBUG,
    .enabled   = true,
    .console   = true,              /* CLI lives on this UART */
};

bool log_sink_uart_register(void)
{
    return log_sink_register(&uart_sink);
}
