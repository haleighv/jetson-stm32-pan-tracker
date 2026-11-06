/*
 * can.h
 *
 * FDCAN1 wrapper. CubeMX owns the peripheral config in Core/Src/fdcan.c
 * (classic CAN, 1 Mbps, 80.8% sample point); this module owns filters,
 * TX/RX, stats, and the loopback self-test.
 *
 * Notes:
 * - FDCAN1 interrupt 0 runs at NVIC priority 6, inside the FreeRTOS
 *   syscall range, so the RX callback can use the ...FromISR() calls.
 * - Counters are written by the RX callback and can_send(); tasks read
 *   them with can_get_stats(), which takes a consistent snapshot.
 * - Milestone 1 runs in internal loopback (no pins, no transceiver).
 * - Classic CAN, standard 11-bit ids, 8-byte frames. FD isn't needed: a
 *   setpoint plus a counter fits in 8 bytes.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef CAN_H
#define CAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tx_frames;     /* queued to the TX FIFO */
    uint32_t tx_full;       /* send rejected, TX FIFO full */
    uint32_t rx_frames;     /* received into RX FIFO0 */
    uint32_t rx_seq_gaps;   /* sequence number skipped (lost frame) */
    uint32_t rx_bad_data;   /* payload didn't match the test pattern */
    uint8_t  tec;           /* transmit error counter */
    uint8_t  rec;           /* receive error counter */
    bool     error_passive;
    bool     bus_off;
} can_stats_t;

/* Filters, RX callback, RX notification, then start. Register before
 * start, same as TIM6. Call once from the supervisor. */
void can_start(void);

/* Queue one classic frame (standard 11-bit id, len 0..8). Returns false
 * if the TX FIFO is full or the HAL refuses it. */
bool can_send(uint32_t id, const uint8_t *data, uint8_t len);

/* Snapshot of the counters plus the controller's error state (TEC, REC,
 * error passive, bus off). Task context. */
void can_get_stats(can_stats_t *stats);

/* Zero the counters. Task context. */
void can_reset_stats(void);

/* Loopback self-test: send num_frames, one in flight at a time. Each RX
 * queues the next TX (ping-pong), so it runs in the ISR and this returns
 * right away. Resets the stats first. Returns false if num_frames is 0, a
 * test is already running, or the first frame couldn't be sent. */
bool can_test_start(uint32_t num_frames);

/* True once the last test frame came back, the test was stopped (a send
 * failed), or no test was ever run. rx_frames == num_frames means it
 * finished cleanly. */
bool can_test_done(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_H */
