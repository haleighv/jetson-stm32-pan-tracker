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
 * - Every run has a deadline (default 1 s, max 5 s) until the Jetson
 *   heartbeat exists. motor_poll() enforces it.
 * - Faults: a falling edge on EN/DIAG (EXTI line 13, routed to port E by
 *   hand since CubeMX can't make PE13 both an output and an EXTI pin)
 *   safes the bridge in the ISR and latches. motor_poll() is the backup for
 *   a line that's already low at boot. Only motor_clear_fault() unlatches.
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

typedef struct {
    int16_t  duty_permille;  /* -1000..1000, 0 when stopped */
    int32_t  position;       /* encoder counts since motor_zero() */
    uint16_t current_ma;     /* last CS reading */
    bool     fault;          /* latched by the EXTI ISR or the poll */
    uint32_t fault_count;    /* faults latched since boot */
    uint32_t ms_remaining;   /* time left on the current run, 0 if stopped */
    bool     run_active;     /* run active flag */
} motor_status_s;

typedef enum {
    MOTOR_REQUEST_RUN = 0,
    MOTOR_REQUEST_STOP,
    MOTOR_REQUEST_ZERO,
} motor_request_type_t;

typedef struct {
    motor_request_type_t type;
    int16_t duty_permille;
    uint32_t run_ticks;  /* 1 tick = 1 ms */
} motor_request_s;

/* Start TIM1 PWM at 0%, start the TIM4 encoder, calibrate ADC3, set up
 * the EN/DIAG falling-edge interrupt, then release EN last. Call once from
 * the supervisor after can_start(). */
void motor_start(void);

/* Set direction and duty, -1000..1000 permille. Sets INA/INB before
 * raising the duty. Returns false if out of range or a fault is latched. */
bool motor_set(int16_t duty_permille);

/* motor_set() with a deadline. run_ms 0 means the default; clamped to
 * MOTOR_RUN_MS_MAX. */
bool motor_run(int16_t duty_permille, uint32_t run_ms);

/* Duty 0, INA = INB = 0 (brake to GND). Clears the deadline. */
void motor_stop(void);

/* Call every 10 ms from the supervisor: extends the 16-bit encoder count,
 * latches a fault if EN is released but reads low (backup for the ISR),
 * and stops the motor when the run deadline passes. */
void motor_poll(void);

/* Encoder counts, extended past 16 bits. */
int32_t motor_position(void);

/* Set the position to 0. */
void motor_zero(void);

/* One ADC3 conversion, mV / 0.14. Only valid while the bridge drives. */
uint16_t motor_current_ma(void);

/* A fault is latched (set by the EXTI ISR or motor_poll()). */
bool motor_fault(void);

/* Clear the latch, only if EN is released (ODR 1) and reads high (IDR 1).
 * Returns false and stays latched otherwise. */
bool motor_clear_fault(void);

/* Snapshot for the CLI. */
void motor_get_status(motor_status_s *status);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */
