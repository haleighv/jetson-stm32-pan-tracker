/*
 * can.c
 *
 * FDCAN1 filters, TX/RX, stats, and the loopback self-test. See can.h.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#define LOG_TAG "can"
#include "can.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "fdcan.h"

#define CAN_TEST_ID     0x7F0   /* high id: loses arbitration to real traffic later */
#define CAN_TEST_LEN    8

/* Counters. Written by the RX callback and can_send() (which the callback
 * calls during a test); tasks read them through can_get_stats(). */
static volatile can_stats_t stats;

/* Self-test state. test_remaining counts frames still to *receive*. Only
 * the RX callback changes these once a test is running. */
static volatile uint32_t test_remaining;
static volatile uint32_t test_next_seq;

/* --- Test pattern ---
 *
 * Each test frame carries its sequence number so the ISR can spot a lost
 * frame (seq jumps ahead) or a mangled one (halves don't agree):
 *
 *   bytes 0..3  seq, little endian (same as memcpy on the M7)
 *   bytes 4..7  bitwise NOT of bytes 0..3
 *
 * The NOT half means every bit goes out once as 1 and once as 0, so a bit
 * stuck either way breaks the match. The hardware CRC already covers the
 * wire; this catches my own RX-path mistakes (wrong buffer, stale read).
 */

static void test_frame_fill(uint8_t data[CAN_TEST_LEN], uint32_t seq)
{
    for (uint8_t i = 0; i < 4; i++) {
        data[i]     = (uint8_t)(seq >> (8U * i));
        data[i + 4] = (uint8_t)~data[i];
    }
}

/* Writes *seq only if the frame checks out */
static bool test_frame_check(const uint8_t data[CAN_TEST_LEN], uint32_t *seq)
{
    uint32_t s = 0;
    for (uint8_t i = 0; i < 4; i++) {
        if (data[i + 4] != (uint8_t)~data[i]) {
            return false;
        }
        s |= (uint32_t)data[i] << (8U * i);
    }
    *seq = s;
    return true;
}

/* --- ISR --- */

/* RX FIFO0 new message. FDCAN1 IT0 is at NVIC priority 6, inside the
 * FreeRTOS syscall range, so ...FromISR() calls are allowed. No logging,
 * nothing that waits.
 *
 * Ping-pong: each test frame that comes back queues the next one, so
 * there's only ever one in flight. */
static void can_rx_fifo0_isr(FDCAN_HandleTypeDef *hfdcan, uint32_t rx_fifo0_its)
{
    (void)rx_fifo0_its;     /* only "new message" is enabled for now */

    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    /* Drain FIFO0: one interrupt can cover several frames */
    while (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &hdr, data) == HAL_OK) {
        uint32_t seq;
        stats.rx_frames++;

        if (hdr.IdType != FDCAN_STANDARD_ID ||
            hdr.Identifier != CAN_TEST_ID ||
            hdr.DataLength != CAN_TEST_LEN) {
            /* TODO (milestone 2): publish real traffic on the message bus (EVENT_CAN_RX) */
            continue;   /* not a test frame */
        }

        if (!test_frame_check(data, &seq)) {
            stats.rx_bad_data++;
            seq = test_next_seq;    /* can't trust seq, assume it was the expected one */
        }

        /* Only count forward jumps as lost frames. seq < expected is a
         * duplicate or out-of-order frame; the subtraction would wrap. */
        if (seq > test_next_seq) {
            stats.rx_seq_gaps += seq - test_next_seq;
        }
        test_next_seq = seq + 1;

        if (test_remaining == 0) {
            continue;   /* stray test frame with no test running; don't wrap */
        }
        test_remaining--;

        if (test_remaining > 0) {
            uint8_t tx[CAN_TEST_LEN];
            test_frame_fill(tx, test_next_seq);
            if (!can_send(CAN_TEST_ID, tx, CAN_TEST_LEN)) {
                test_remaining = 0;     /* nothing in flight now, so stop the test */
            }
        }
    }
}

/* --- API --- */

void can_start(void)
{
    /* Accept every standard id into FIFO0. Mask filter: a 1 in FilterID2
     * means "this bit must match FilterID1", so a mask of 0 matches all. */
    const FDCAN_FilterTypeDef *filter_config = &(FDCAN_FilterTypeDef){
        .IdType = FDCAN_STANDARD_ID,
        .FilterIndex = 0,                       /* Std Filters Nbr = 1 in CubeMX */
        .FilterType = FDCAN_FILTER_MASK,
        .FilterConfig = FDCAN_FILTER_TO_RXFIFO0,
        .FilterID1 = 0x000,                     /* id */
        .FilterID2 = 0x000                      /* mask */
    };
    if (HAL_FDCAN_ConfigFilter(&hfdcan1, filter_config) != HAL_OK) {
        LOG_E("filter config failed");
        return;
    }

    /* Anything that matches no filter, and remote frames, gets dropped */
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
        FDCAN_REJECT,           /* std ids that matched no filter */
        FDCAN_REJECT,           /* ext ids that matched no filter */
        FDCAN_REJECT_REMOTE,    /* std remote frames */
        FDCAN_REJECT_REMOTE) != HAL_OK) {  /* ext remote frames */
        LOG_E("global filter config failed");
        return;
    }

    /* Register before start, same as TIM6 */
    if (HAL_FDCAN_RegisterRxFifo0Callback(&hfdcan1, can_rx_fifo0_isr) != HAL_OK) {
        LOG_E("rx callback register failed");
        return;
    }

    /* Last arg is BufferIndexes, only used for TX buffer interrupts */
    if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        LOG_E("rx notification enable failed");
        return;
    }

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
        LOG_E("fdcan start failed");
        return;
    }
    LOG_I("can started");
}

/* Called from tasks and from the RX callback. Doesn't log: it can run in
 * the ISR, and a full TX FIFO is normal under load (tx_full counts it). */
bool can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (len > 8 || id > 0x7FF) {
        return false;
    }

    FDCAN_TxHeaderTypeDef tx_header = {
        .Identifier = id,
        .IdType = FDCAN_STANDARD_ID,
        .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = len,      /* this HAL: FDCAN_DLC_BYTES_n == n for 0..8 */
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch = FDCAN_BRS_OFF,
        .FDFormat = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
        .MessageMarker = 0
    };
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_header, data) != HAL_OK) {
        stats.tx_full++;
        return false;
    }
    stats.tx_frames++;
    return true;
}

void can_get_stats(can_stats_t *out)
{
    /* Critical section masks the FDCAN interrupt (priority 6), so the
     * counters come out as one consistent snapshot, not half-updated. */
    taskENTER_CRITICAL();
    *out = stats;
    taskEXIT_CRITICAL();

    /* Straight from the controller's registers */
    FDCAN_ErrorCountersTypeDef ec;
    FDCAN_ProtocolStatusTypeDef ps;
    if (HAL_FDCAN_GetErrorCounters(&hfdcan1, &ec) == HAL_OK) {
        out->tec = (uint8_t)ec.TxErrorCnt;
        out->rec = (uint8_t)ec.RxErrorCnt;
    }
    if (HAL_FDCAN_GetProtocolStatus(&hfdcan1, &ps) == HAL_OK) {
        out->error_passive = (ps.ErrorPassive != 0U);
        out->bus_off       = (ps.BusOff != 0U);
    }
}

void can_reset_stats(void)
{
    taskENTER_CRITICAL();
    stats = (can_stats_t){0};
    taskEXIT_CRITICAL();
}

bool can_test_start(uint32_t num_frames)
{
    if (num_frames == 0 || test_remaining != 0) {
        return false;   /* nothing to do, or a test is already running */
    }

    can_reset_stats();

    uint8_t tx[CAN_TEST_LEN];
    test_frame_fill(tx, 0);

    /* Set the state before the first send: in loopback the frame can come
     * back before can_send() returns. The critical section holds off the RX
     * callback until we're done, so it never sees a half-started test or
     * races us on tx_frames. */
    bool sent;
    taskENTER_CRITICAL();
    test_next_seq  = 0;
    test_remaining = num_frames;
    sent = can_send(CAN_TEST_ID, tx, CAN_TEST_LEN);
    if (!sent) {
        test_remaining = 0;
    }
    taskEXIT_CRITICAL();

    return sent;
}

bool can_test_done(void)
{
    return test_remaining == 0;
}
