/*
 * motor.c
 *
 * VNH5019 direction/duty, encoder position, current sense, fault line,
 * and the run deadline. See motor.h.
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

static volatile int16_t       duty;                    /* last commanded duty, permille */
static volatile int32_t       position = 0;            /* extended encoder count */
static uint16_t               last_cnt = 0;            /* TIM4 CNT at the previous read */
static volatile uint32_t      run_ticks_remaining = 0;
static volatile bool          motor_started = false;
static volatile bool          fault_latched;           /* set by the EXTI ISR or the poll */
static volatile uint32_t      fault_count;
static volatile motor_state_t state = MOTOR_STATE_UNINITIALIZED;
static bool volatile          poll_fault_detected;
static bool volatile          run_complete = false;

static motor_request_s   motor_request = {0};        /* writer: task; reader: motor tick handler */
static volatile uint32_t motor_request_sequence = 0; /* odd during the write, even otherwise */
static uint32_t          motor_request_applied = 0;  /* used by motor tick handler only: last request applied */

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

/* Post a request to the tick handler isr (no FreeRTOS calls allowed) for motor control.
 * Ensure that the request mod operation is atomic to guard competing requests */
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

/* Helper function for checking request validity */
static bool is_valid_new_request(void)
{
    return (!(motor_request_sequence % 2) && (motor_request_sequence != motor_request_applied));
}

/* Helper function for filtering requests */
static void filter_request(void)
{
    if (is_valid_new_request()) {
        /* update the applied request id without perfoming request action */
        motor_request_applied = motor_request_sequence;
    }
}

/* Normal stop: short the motor through both low sides (Table 12, PWM high).
 * Task context only, inside taskENTER_CRITICAL(). */
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
 * Any context, including the EXTI ISR: no logging, no FreeRTOS calls, no
 * critical section here. Task-side callers wrap it in taskENTER_CRITICAL(). */
static void bridge_coast(void)
{
    TIM1->CCR1 = 0;                 /* PWM low: LSA and LSB off */
    HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    duty = 0;
    state = MOTOR_STATE_COAST;
}

static void motor_set(int16_t duty_permille)
{
    duty = duty_permille;
    /* Direction pins first, then the duty */
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
    /* Start in brake state (the EXTI is live now, so take the lock) */
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

void motor_control_tick_handler(void)
{
    static uint8_t reversal_brake_ticks = 0;

    if (!motor_started) {
        return;
    }

    /* Position Update */
    motor_update_position();

    /* Poll for a fault - second line of defense to EN interrupt */
    if ((en_state() == EN_FAULT) && (!fault_latched)) {
        poll_fault_detected = true;
        fault_latched = true;
        fault_count++;
    }
    /* Fault Responses */
    if (fault_latched) {
        if (state != MOTOR_STATE_COAST) {
            bridge_coast();
        }
        filter_request();
        return;
    }

    /* Ongoing Activity */
    if ((state == MOTOR_STATE_RUNNING_POS) || (state == MOTOR_STATE_RUNNING_NEG)) {
        run_ticks_remaining--;
        if (run_ticks_remaining == 0) {
            bridge_brake();
            run_complete = true;
        }
    }

    /* Request Handling */
    if (is_valid_new_request()) {
        switch (motor_request.type) {
            case MOTOR_REQUEST_RUN:
            {
                /* Ignore run requests with duty == 0 */
                if (!motor_request.duty_permille) {
                    filter_request();
                    return;
                }
                motor_state_t req_state = motor_request.duty_permille > 0 ?
                    MOTOR_STATE_RUNNING_POS : MOTOR_STATE_RUNNING_NEG;
                /* Upon a running reversal, brake the motor for REVERSAL_BRAKE_TICKS_DEFAULT ticks */
                if ((state == MOTOR_STATE_RUNNING_POS) || (state == MOTOR_STATE_RUNNING_NEG)) { /* currently running */
                    if (req_state != state) { /* reversal */
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
                /* Run the motor and update the state */
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
                position = 0;
                break;
            default:
                break;
        }
        /* update the applied request id */
        motor_request_applied = motor_request_sequence;
    }
    return;
}

/* ISR context: safe the bridge first, then latch and tell the supervisor */
static void motor_fault_isr(void)
{
    BaseType_t woken = pdFALSE;
    msg_s m = { .id = EVENT_MOTOR_FAULT };
    bool was_fault_latched = fault_latched;
    fault_latched = true;
    bridge_coast();
    /* filter additional fault triggers if latched */
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

