/*
 * motor.c
 *
 * VNH5019 direction/duty, encoder position, current sense, fault line,
 * and the run deadline. See motor.h.
 *
 * Single writer: motor_control_tick_handler() (TIM6, NVIC 4) owns the
 * bridge and the position. Tasks post requests; the fault ISR (NVIC 5,
 * below the tick) only latches and coasts. motor_start() writes the
 * bridge once, before it enables the tick's handling.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#define LOG_TAG "motor"
#include <stdint.h>
#include "motor.h"
#include "log.h"
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "tim.h"
#include "adc.h"
#include "msg.h"
#include "events.h"

#define CS_MV_PER_A                  140U    /* Pololu carrier, ~140 mV/A */
#define ADC_FULL                     4095U   /* 12-bit */
#define VREF_MV                      3300U
#define REVERSAL_BRAKE_TICKS_DEFAULT 2

/* EN/DIAG line state: what we drive (ODR) vs what the pin reads (IDR).
 * Open drain, so only the VNH5019, the e-stop or a short can pull a
 * released line low. */
typedef enum {
    EN_ENABLED = 0,  /* released, reads high: driver on */
    EN_FAULT,        /* released, reads low: VNH5019 fault or e-stop */
    EN_DISABLED,     /* we drive it low */
    EN_STUCK_HIGH,   /* we drive it low but it reads high: wiring fault */
} en_state_t;

/* Bridge and position: written by the tick (and the fault ISR via
 * bridge_coast()), read by tasks */
static volatile int16_t       duty;                    /* last commanded duty, permille */
static volatile int32_t       position = 0;            /* extended encoder count */
static uint16_t               last_cnt = 0;            /* tick only: TIM4 CNT at the previous tick */
static volatile uint32_t      run_ticks_remaining = 0; /* ms left on the current run, 0 if none */
static volatile motor_state_t state = MOTOR_STATE_UNINITIALIZED;

static volatile bool          motor_started = false;   /* set last in motor_start(): gates the tick */
static volatile bool          fault_latched;           /* set by the EXTI ISR or the tick's check, cleared by motor_clear_fault() */
static volatile uint32_t      fault_count;

/* Tick -> motor_poll() flags: the tick sets, motor_poll() acts then clears */
static volatile bool          poll_fault_detected;     /* fault found by the tick's backup check */
static volatile bool          run_complete = false;    /* run deadline reached */

/* Task -> tick requests (see post_motor_ctrl_request()) */
static motor_request_s   motor_request = {0};        /* written by tasks, read by the tick */
static volatile uint32_t motor_request_sequence = 0; /* odd while a task is writing, even otherwise */
static uint32_t          motor_request_applied = 0;  /* tick only: last even value applied */

/* Reads ODR and IDR once each. Register reads only: safe in any context. */
static en_state_t en_state(void)
{
    bool released = (MOTOR_EN_GPIO_Port->ODR & MOTOR_EN_Pin) != 0U;
    bool high     = (MOTOR_EN_GPIO_Port->IDR & MOTOR_EN_Pin) != 0U;

    if (released) {
        return high ? EN_ENABLED : EN_FAULT;
    }
    return high ? EN_STUCK_HIGH : EN_DISABLED;
}

/* Drive EN low: the driver stays off whatever INA/INB/PWM say */
static void en_disable(void)
{
    HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
}

/* Release EN (open drain): the driver turns on unless something holds it low */
static void en_release(void)
{
    HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_SET);
}

/* Task side of the request record. The tick (NVIC 4) isn't masked by
 * taskENTER_CRITICAL(), so it can run in the middle of this. The sequence
 * counter is odd while the fields are written, and the tick skips an odd
 * value, so it never applies a half-written record. The critical section
 * only serializes tasks against each other. __DMB() keeps the compiler
 * from moving the field writes past the counter. A second request before
 * the next tick replaces the first: the newest one counts. */
static void post_motor_ctrl_request(motor_request_type_t motor_request_type, int16_t duty, uint32_t ticks)
{
    if (!motor_started) {
        LOG_E("motor request failed: motor not yet started");
        return;
    }
    taskENTER_CRITICAL();
    motor_request_sequence++;
    __DMB();
    motor_request.type = motor_request_type;
    motor_request.duty_permille = duty;
    motor_request.run_ticks = ticks;
    __DMB();
    motor_request_sequence++;
    taskEXIT_CRITICAL();
}

/* Tick only: a complete (even) request the tick hasn't applied yet */
static bool is_valid_new_request(void)
{
    return (!(motor_request_sequence % 2) && (motor_request_sequence != motor_request_applied));
}

/* Tick only: mark a pending request applied without acting on it (fault
 * latched, or a zero-duty run), so it can't start later */
static void filter_request(void)
{
    if (is_valid_new_request()) {
        motor_request_applied = motor_request_sequence;
    }
}

/* Normal stop and idle: short the motor through both low sides (Table 12,
 * PWM high). The tick, plus motor_start() before the tick is enabled.
 * Never from the fault ISR: a faulted driver coasts. */
static void bridge_brake(void)
{
    HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    TIM1->CCR1 = TIM1->ARR + 1;     /* PWM held high: LSA and LSB on */
    run_ticks_remaining = 0;
    duty = 0;
    state = MOTOR_STATE_BRAKE;
}

/* Fault: everything off, the motor coasts. Braking would push current
 * through the low sides of a driver that just reported a fault.
 * Called from the fault ISR and the tick: no logging, no FreeRTOS calls,
 * no critical section here. */
static void bridge_coast(void)
{
    TIM1->CCR1 = 0;                 /* PWM low: LSA and LSB off */
    HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    duty = 0;
    state = MOTOR_STATE_COAST;
}

/* Tick only: drive at duty_permille. Direction pins first, then the duty.
 * The tick brakes first on a reversal. */
static void motor_set(int16_t duty_permille)
{
    duty = duty_permille;
    if (duty_permille > 0) {
        HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    } else if (duty_permille < 0) {
        HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    }
    TIM1->CCR1 = (uint32_t)(abs(duty_permille) * (TIM1->ARR + 1) / 1000);
}

/* Tick only: extend the 16-bit TIM4 count. The signed difference handles
 * the wrap in either direction while the counter moves < 32767 counts per
 * tick (about 3 at full speed). */
static void motor_update_position(void)
{
    uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
    position += (int16_t)(now - last_cnt);
    last_cnt = now;
}

void motor_start(void)
{
    /* Start TIM1 PWM at 0%, start the TIM4 encoder, calibrate ADC3, release
     * EN. Call once from the supervisor after can_start(). */
    duty = 0;
    position = 0; /* Assume the encoder is at zero for now until homing is implemented */
    /* On any failure, EN (released at reset) is driven low so the driver
     * stays off, and each step logs its own error */
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) {
        en_disable();
        LOG_E("TIM1 PWM start failed, driver held disabled");
        return;
    }
    if (HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL) != HAL_OK) {
        en_disable();
        LOG_E("TIM4 encoder start failed, driver held disabled");
        return;
    }
    if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        en_disable();
        LOG_E("ADC3 calibration failed, driver held disabled");
        return;
    }
    last_cnt = __HAL_TIM_GET_COUNTER(&htim4);
    /* Configure EXTI line 13 to port E w/ falling edge interrupt */
    EXTI_HandleTypeDef hexti_en;
    EXTI_ConfigTypeDef cfg = {
        .Line = EXTI_LINE_13,
        .Mode = EXTI_MODE_INTERRUPT,
        .Trigger = EXTI_TRIGGER_FALLING,
        .GPIOSel = EXTI_GPIOE,
    };
    if (HAL_EXTI_SetConfigLine(&hexti_en, &cfg) != HAL_OK) {
        en_disable();
        LOG_E("EXTI13 (EN/DIAG) config failed, driver held disabled");
        return;
    }
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    __HAL_GPIO_EXTI_CLEAR_IT(MOTOR_EN_Pin);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    en_release();
    /* Start in brake. The lock masks the EXTI, which is live now; the tick
     * doesn't touch the bridge until motor_started is set, last. */
    taskENTER_CRITICAL();
    bridge_brake();
    taskEXIT_CRITICAL();
    motor_started = true;
    LOG_I("motor started");
}

bool motor_run(int16_t duty_permille, uint32_t run_ms)
{
    if (fault_latched || !motor_started) {
        return false;
    }
    if (duty_permille < -MOTOR_DUTY_MAX || duty_permille > MOTOR_DUTY_MAX) {
        return false;
    }
    if (run_ms > MOTOR_RUN_MS_MAX) {
        run_ms = MOTOR_RUN_MS_MAX;
    }
    if (run_ms == 0) {
        run_ms = MOTOR_RUN_MS_DEFAULT;
    }
    post_motor_ctrl_request(MOTOR_REQUEST_RUN, duty_permille, run_ms);
    LOG_I("MOTOR_REQUEST_RUN");
    return true;
}

void motor_stop(void)
{
    post_motor_ctrl_request(MOTOR_REQUEST_STOP, 0, 0);  /* duty and ticks unused */
    LOG_I("MOTOR_REQUEST_STOP");
}

void motor_poll(void)
{
    if (poll_fault_detected) {
        LOG_E("fault line low (poll)");
        event_post(EVENT_MOTOR_FAULT);
        poll_fault_detected = false;
    }
    if (run_complete) {
        run_complete = false;
        LOG_I("Run deadline passed. Stopped.");
    }
}

int32_t motor_position(void)
{
    return position;
}

void motor_zero(void)
{
    post_motor_ctrl_request(MOTOR_REQUEST_ZERO, 0, 0); /* duty and ticks unused */
    LOG_I("MOTOR_REQUEST_ZERO");
}

uint16_t motor_current_ma(void)
{
    if (HAL_ADC_Start(&hadc3) != HAL_OK) {
        LOG_E("motor_current_ma: ADC3 start failed");
        return 0;
    }
    /* Poll for now, just cli testing; consider DMA later */
    if (HAL_ADC_PollForConversion(&hadc3, 1) != HAL_OK) {
        LOG_E("motor_current_ma: poll failed. getting last result.");
    }
    uint32_t mv = HAL_ADC_GetValue(&hadc3) * VREF_MV / ADC_FULL;
    return (uint16_t)(mv * 1000 / CS_MV_PER_A);
}

bool motor_fault(void)
{
    return fault_latched;
}

bool motor_clear_fault(void)
{
    /* The lock masks the EXTI (NVIC 5), so a new fault can't land between
     * the pin check and the clear. The tick only reads fault_latched, so it
     * doesn't need masking. Post after the lock. */
    taskENTER_CRITICAL();
    if (en_state() != EN_ENABLED) {
        taskEXIT_CRITICAL();
        return false; /* still faulted, or EN not released: keep the latch */
    }
    bool was_latched = fault_latched;
    fault_latched = false;
    taskEXIT_CRITICAL();

    if (was_latched) {
        motor_stop();   /* the tick brakes from COAST */
        event_post(EVENT_MOTOR_FAULT_CLEARED);
    }
    return true;
}

void motor_get_status(motor_status_s *status)
{
    if (!status) {
        return;
    }
    status->duty_permille = duty;
    status->position = position;
    status->current_ma = motor_current_ma();
    status->fault = fault_latched;
    status->fault_count = fault_count;
    status->ms_remaining = run_ticks_remaining;
    status->state = state;
}

/* Order matters: position first (every tick, even while faulted), then
 * faults, then the running deadline, then new requests. */
void motor_control_tick_handler(void)
{
    static uint8_t reversal_brake_ticks = 0;  /* brake ticks left before a reversal */

    if (!motor_started) {
        return;
    }

    motor_update_position();

    /* Backup fault check: catches a line that's low without an edge for the
     * EXTI. motor_poll() logs it and posts the event. */
    if ((en_state() == EN_FAULT) && (!fault_latched)) {
        poll_fault_detected = true;
        fault_latched = true;
        fault_count++;
    }
    /* Fault latched: hold coast and drop requests, so a run posted just
     * before the fault never starts after the clear */
    if (fault_latched) {
        if (state != MOTOR_STATE_COAST) {
            bridge_coast();
        }
        filter_request();
        return;
    }

    /* Run deadline */
    if ((state == MOTOR_STATE_RUNNING_POS) || (state == MOTOR_STATE_RUNNING_NEG)) {
        run_ticks_remaining--;
        if (run_ticks_remaining == 0) {
            bridge_brake();
            run_complete = true;
        }
    }

    /* New request */
    if (is_valid_new_request()) {
        switch (motor_request.type) {
            case MOTOR_REQUEST_RUN:
            {
                /* Zero duty: drop it (motor_stop() is the way to stop) */
                if (!motor_request.duty_permille) {
                    filter_request();
                    return;
                }
                motor_state_t req_state = motor_request.duty_permille > 0 ?
                    MOTOR_STATE_RUNNING_POS : MOTOR_STATE_RUNNING_NEG;
                /* Reversal: brake for REVERSAL_BRAKE_TICKS_DEFAULT ticks first.
                 * The RUN stays pending (not marked applied) and is applied
                 * here once the count runs out. */
                if ((state == MOTOR_STATE_RUNNING_POS) || (state == MOTOR_STATE_RUNNING_NEG)) {
                    if (req_state != state) {
                        reversal_brake_ticks = REVERSAL_BRAKE_TICKS_DEFAULT;
                        bridge_brake();
                        return;
                    }
                } else if (state == MOTOR_STATE_BRAKE) {
                    if (reversal_brake_ticks > 0) {
                        if (--reversal_brake_ticks > 0) {
                            return;     /* still braking; the RUN stays pending */
                        }
                    }
                }
                motor_set(motor_request.duty_permille);
                run_ticks_remaining = motor_request.run_ticks;
                state = req_state;
                break;
            }
            case MOTOR_REQUEST_STOP:
                run_ticks_remaining = 0;
                if (state != MOTOR_STATE_BRAKE) {
                    bridge_brake();
                }
                break;
            case MOTOR_REQUEST_ZERO:
                position = 0;   /* last_cnt was updated at the top of this tick */
                break;
            default:
                break;
        }
        motor_request_applied = motor_request_sequence;
    }
    return;
}

/* EXTI context (NVIC 5). Latch first: the tick (NVIC 4) can interrupt
 * between these lines, and it checks the latch before driving. Then coast.
 * Count and post only on the first edge of a latch. If the tick's backup
 * check latches between the read and the write below, one fault counts
 * twice and posts twice; harmless. */
static void motor_fault_isr(void)
{
    BaseType_t woken = pdFALSE;
    msg_s m = { .id = EVENT_MOTOR_FAULT };
    bool was_fault_latched = fault_latched;
    fault_latched = true;
    bridge_coast();
    if (was_fault_latched) {
        return;
    }
    fault_count++;
    msg_post_from_isr(&m, &woken);
    portYIELD_FROM_ISR(woken);
}

/* EXTI lines 10-15 share this vector. Only line 13 (PE13, EN/DIAG) is
 * enabled (for now); the GET_IT check keeps it that way if another line is
 * added later. Priority 5: below the control tick, so the tick can
 * interrupt this handler. */
void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(MOTOR_EN_Pin) != 0U) {
        /* The pending bit holds the request until cleared; if left set,
         * the NVIC re-enters this handler forever. Clear first, so an
         * edge during motor_fault_isr() sets it again and isn't lost.
         * Edges while it's set merge into one interrupt. */
        __HAL_GPIO_EXTI_CLEAR_IT(MOTOR_EN_Pin);

        /* Fault only if EN is released (ODR 1) and the line still reads
         * low: our own EN writes and glitches shorter than interrupt
         * entry don't latch. The VNH5019 and the e-stop hold it low. */
        if (en_state() == EN_FAULT) {
            motor_fault_isr();
        }
    }
}

