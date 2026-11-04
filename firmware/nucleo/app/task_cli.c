/*
 * task_cli.c
 *
 * CLI over USART3. The RX interrupt pushes each byte into a stream buffer;
 * this task feeds them to the CLI core (lib/cli.c). Echo and command
 * output go through log_console(), so the log task stays the only thing
 * writing to the UART.
 *
 * Notes:
 * - Needs the USART3 global interrupt enabled in CubeMX, at a priority
 *   that can call FreeRTOS FromISR APIs (5 or higher number).
 * - HAL_UART_RxCpltCallback / ErrorCallback live here. If another UART
 *   needs them later, move them to a shared file and dispatch by instance.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "cli"
#include "task_cli.h"
#include "cli.h"
#include "cli_cmds.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "usart.h"

#define CLI_RX_BUF_SIZE     64      /* bytes buffered between ISR and task */

static StreamBufferHandle_t rx_stream;
static uint8_t              rx_byte;    /* HAL receives one byte at a time into here */
static cli_t                cli;

static void cli_write(const char *s, size_t len)
{
    log_console(s, len);
}

/* Arm reception of the next byte */
static void rx_start(void)
{
    (void)HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
}

void task_cli(void *arg)
{
    (void)arg;

    rx_stream = xStreamBufferCreate(CLI_RX_BUF_SIZE, 1);
    configASSERT(rx_stream != NULL);

    cli_init(&cli, cli_cmds, cli_cmds_count, cli_write, "> ");
    rx_start();

    LOG_I("ready, type '?' for help");
    cli_prompt(&cli);

    for (;;) {
        char c;
        if (xStreamBufferReceive(rx_stream, &c, 1, portMAX_DELAY) == 1) {
            cli_input(&cli, c);
        }
    }
}

/* --- HAL callbacks (ISR context) --- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) {
        return;
    }

    BaseType_t woken = pdFALSE;
    if (rx_stream != NULL) {
        (void)xStreamBufferSendFromISR(rx_stream, &rx_byte, 1, &woken);
    }
    rx_start();
    portYIELD_FROM_ISR(woken);      /* run the CLI task now, not at the next tick */
}

/* Overrun/framing/noise errors can stop reception. Re-arm so the CLI
 * doesn't go deaf after a burst of input. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        rx_start();
    }
}
