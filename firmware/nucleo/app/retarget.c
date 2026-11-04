/*
 * retarget.c
 *
 * Sends stdout (printf) out USART3, which the ST-LINK forwards to the Mac
 * as /dev/cu.usbmodem*. Blocking, so it's for early bring-up only; the log
 * task replaces this later.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "usart.h"

/* Overrides the weak _write() in Core/Src/syscalls.c. newlib calls this
 * for stdout/stderr. Sends the whole buffer instead of one char at a time. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart3, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}
