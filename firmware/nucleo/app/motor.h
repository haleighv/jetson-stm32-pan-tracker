/*
 * motor.h
 *
 * Pololu VNH5019 + goBILDA 5203 driver. CubeMX owns the peripheral config
 * (Core/Src/tim.c, adc.c, gpio.c); this module owns direction, duty, the
 * encoder position, current sense, the fault line, and the auto-stop.
 *
 * Pins:
 * - PE9  TIM1_CH1  PWM, 20 kHz (ARR 12999 at 260 MHz)
 * - PE11 INA, PE14 INB  direction, push-pull
 * - PE13 EN/DIAG (ENA and ENB tied), open drain, released = enabled
 * - PF4  ADC3_INP9  CS through 1 kΩ + 100 nF, ~140 mV/A
 * - PD12/PD13 TIM4_CH1/CH2  encoder, x4, 751.8 counts per output rev
 *
 * Notes:
 * - Sign convention: positive duty sets INA high, INB low (OUTA high) and
 *   turns the output shaft counterclockwise, viewed from the shaft end.
 *   Positive duty gives increasing encoder counts. The count sign is set
 *   in one place: TIM4 CH1 polarity Falling in CubeMX (nucleo.ioc).
 *   Counterclockwise positive matches ROS REP 103 (positive rotation is
 *   counterclockwise about an axis pointing up) with the motor mounted
 *   shaft-up.
 * - The control tick (motor_control_tick_handler(), TIM6 ISR, 1 kHz, NVIC
 *   priority 4) is the only writer of the bridge (INA, INB, CCR1) and the
 *   position. Task-side calls post a request that the tick applies on its
 *   next tick, so their effect shows up within 1 ms. FreeRTOS critical
 *   sections don't mask the tick; see post_motor_ctrl_request() in motor.c.
 * - Every run has a deadline (default 1 s, max 5 s) until the Jetson
 *   heartbeat exists. The tick counts it down and brakes at zero.
 * - Faults: a falling edge on EN/DIAG (EXTI line 13, routed to port E by
 *   hand since CubeMX can't make PE13 both an output and an EXTI pin)
 *   latches and coasts the bridge in the ISR. The tick's own check is the
 *   backup, for a line that's already low with no edge (e.g. at start).
 *   While latched, the tick holds coast and drops requests. Only
 *   motor_clear_fault() unlatches; the tick then brakes.
 * - EXTI line 13 belongs to PE13, so B1 (PC13) can't use an interrupt.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTOR_DUTY_MAX       1000    /* permille */
#define MOTOR_RUN_MS_DEFAULT 1000
#define MOTOR_RUN_MS_MAX     5000

/* Bridge state, as last set by the tick (or the fault ISR for COAST) */
typedef enum {
    MOTOR_STATE_UNINITIALIZED = 0,  /* before motor_start() */
    MOTOR_STATE_RUNNING_POS,        /* driving, positive duty (counterclockwise) */
    MOTOR_STATE_RUNNING_NEG,        /* driving, negative duty */
    MOTOR_STATE_BRAKE,              /* INA = INB = 0, PWM high: idle */
    MOTOR_STATE_COAST,              /* everything off: fault latched */
} motor_state_t;

typedef struct {
    int16_t  duty_permille;  /* -1000..1000, 0 when stopped */
    int32_t  position;       /* encoder counts since motor_zero() */
    uint16_t current_ma;     /* last CS reading */
    bool     fault;          /* latched by the EXTI ISR or the tick's check */
    uint32_t fault_count;    /* faults latched since boot */
    uint32_t ms_remaining;   /* time left on the current run, 0 if stopped */
    motor_state_t state;     /* bridge state */
} motor_status_s;

typedef enum {
    MOTOR_REQUEST_RUN = 0,
    MOTOR_REQUEST_STOP,
    MOTOR_REQUEST_ZERO,
} motor_request_type_t;

/* Task -> tick request record (see post_motor_ctrl_request() in motor.c) */
typedef struct {
    motor_request_type_t type;
    int16_t duty_permille;  /* RUN only */
    uint32_t run_ticks;     /* RUN only; 1 tick = 1 ms */
} motor_request_s;

/* Start TIM1 PWM at 0%, start the TIM4 encoder, calibrate ADC3, set up
 * the EN/DIAG falling-edge interrupt, release EN, brake, then enable the
 * tick's motor handling last. Call once from the supervisor after
 * can_start(). On failure, EN is driven low and the motor stays off. */
void motor_start(void);

/* Post a run: duty -1000..1000 permille (sign = direction) for run_ms
 * (0 = MOTOR_RUN_MS_DEFAULT, clamped to MOTOR_RUN_MS_MAX). The tick brakes
 * for 2 ms first on a reversal. Returns false if the duty is out of range,
 * a fault is latched, or the motor isn't started. */
bool motor_run(int16_t duty_permille, uint32_t run_ms);

/* Post a stop: the tick brakes (INA = INB = 0, PWM high) and ends the run. */
void motor_stop(void);

/* Call every 10 ms from the supervisor. Reports what the tick can't (no
 * FreeRTOS calls there): logs a finished run, and logs and posts
 * EVENT_MOTOR_FAULT for a fault found by the tick's backup check. */
void motor_poll(void);

/* Encoder counts since motor_zero(), extended past 16 bits by the tick. */
int32_t motor_position(void);

/* Post a zero: the tick sets the position to 0 on its next tick. */
void motor_zero(void);

/* One ADC3 conversion, mV / 0.14. Only valid while the bridge drives. */
uint16_t motor_current_ma(void);

/* A fault is latched (set by the EXTI ISR or the tick's backup check). */
bool motor_fault(void);

/* Clear the latch, only if EN is released (ODR 1) and reads high (IDR 1),
 * then post a stop so the tick brakes from coast. Returns false and stays
 * latched otherwise. */
bool motor_clear_fault(void);

/* Snapshot for the CLI. */
void motor_get_status(motor_status_s *status);

/* Control tick only (TIM6 ISR, 1 kHz, NVIC priority 4): no FreeRTOS calls.
 * Extends the position, runs the backup fault check, holds coast while
 * latched, counts down the run, and applies posted requests. */
void motor_control_tick_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */
