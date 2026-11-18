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

#define CS_MV_PER_A   140U    /* Pololu carrier, ~140 mV/A */
#define ADC_FULL      4095U   /* 12-bit */
#define VREF_MV       3300U

static int16_t           duty;            /* last commanded duty, permille */
static int32_t           position = 0;        /* extended encoder count */
static uint16_t          last_cnt = 0;        /* TIM4 CNT at the previous read */
static TickType_t        run_deadline;    /* valid while run_active */
static bool              run_active = false;
static bool              motor_started = false;
static volatile bool     fault_latched;  /* set by the EXTI ISR or the poll */
static volatile uint32_t fault_count;

/* Normal stop: short the motor through both low sides (Table 12, PWM high).
 * Task context only, inside taskENTER_CRITICAL(). */
static void bridge_brake(void)
{
    HAL_GPIO_WritePin(MOTOR_INA_GPIO_Port, MOTOR_INA_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_GPIO_Port, MOTOR_INB_Pin, GPIO_PIN_RESET);
    TIM1->CCR1 = TIM1->ARR + 1;     /* PWM held high: LSA and LSB on */
    run_active = false;
    duty = 0;
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
    run_active = false;
    duty = 0;
}

/* EN released (ODR 1) but the VNH5019 pulls the line low (IDR 0) */
static bool fault_live(void)
{
    return ((MOTOR_EN_GPIO_Port->ODR & MOTOR_EN_Pin) != 0 &&
            HAL_GPIO_ReadPin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin) == GPIO_PIN_RESET) ?
                true :
                false;
}

/* ISR context: safe the bridge first, then latch and tell the supervisor */
static void motor_fault_isr(void)
{
    BaseType_t woken = pdFALSE;
    msg_t m = { .id = EVENT_MOTOR_FAULT };
    bridge_coast();
    /* filter additional fault triggers if latched */
    if (fault_latched) {
        return;
    }
    fault_latched = true;
    fault_count++;
    msg_post_from_isr(&m, &woken);
    portYIELD_FROM_ISR(woken);
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
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
        LOG_E("TIM1 PWM start failed, driver held disabled");
        return;
    }
    if (HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL) != HAL_OK) {
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
        LOG_E("TIM4 encoder start failed, driver held disabled");
        return;
    }
    if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
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
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
        LOG_E("EXTI13 (EN/DIAG) config failed, driver held disabled");
        return;
    }
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    __HAL_GPIO_EXTI_CLEAR_IT(MOTOR_EN_Pin);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_SET);
    motor_started = true;
    /* Start in brake state (the EXTI is live now, so take the lock) */
    taskENTER_CRITICAL();
    bridge_brake();
    taskEXIT_CRITICAL();
    LOG_I("motor started");
}

bool motor_set(int16_t duty_permille)
{
    if (!motor_started) {
        LOG_E("motor set failed: motor not yet started");
        return false;
    }
    if (duty_permille < -MOTOR_DUTY_MAX || duty_permille > MOTOR_DUTY_MAX) {
        return false;
    }
    /* Reversal: brake, then let at least one PWM period pass. Outside the
     * critical section, since vTaskDelay() can't run with interrupts masked. */
    if (((duty_permille < 0) && (duty > 0)) || ((duty_permille > 0) && (duty < 0))) {
        taskENTER_CRITICAL();
        bridge_brake();
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    /* Masks the EXTI (priority 5): a fault can't land between the latch
     * check and the writes, which would re-toggle INA/INB and clear the
     * VNH5019's own latch. No logging or other FreeRTOS calls in here. */
    taskENTER_CRITICAL();
    if (fault_latched) {
        taskEXIT_CRITICAL();
        LOG_E("Refusing to set motor. fault latched.");
        return false;
    }
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
    taskEXIT_CRITICAL();
    LOG_I("Motor set to %d", duty_permille);
    return true;
}

bool motor_run(int16_t duty_permille, uint32_t run_ms)
{
    if (duty_permille < -MOTOR_DUTY_MAX || duty_permille > MOTOR_DUTY_MAX) {
        return false;
    }
    if (run_ms > MOTOR_RUN_MS_MAX) {
        run_ms = MOTOR_RUN_MS_MAX;
    }
    if (run_ms == 0) {
        run_ms = MOTOR_RUN_MS_DEFAULT;
    }
    if (!motor_set(duty_permille)) {
        LOG_E("Motor set failed");
        return false;
    }
    run_deadline = xTaskGetTickCount() + run_ms;
    run_active = true;
    return true;
}

void motor_stop(void)
{
    taskENTER_CRITICAL();
    bridge_brake();
    taskEXIT_CRITICAL();
    LOG_I("Motor Stop");
}

void motor_poll(void)
{
    TickType_t now = xTaskGetTickCount();
    bool new_fault = false;
    bool expired = false;

    /* Decide and act under the lock; log and post after it */
    taskENTER_CRITICAL();
    motor_position();
    if (fault_live() && !fault_latched) {
        bridge_coast();
        fault_latched = true;
        fault_count++;
        new_fault = true;
    }
    if (run_active && (int32_t)(now - run_deadline) >= 0) {
        bridge_brake();
        expired = true;
    }
    taskEXIT_CRITICAL();

    if (new_fault) {
        LOG_E("fault line low (poll)");
        event_post(EVENT_MOTOR_FAULT);
    }
    if (expired) {
        LOG_I("Run deadline passed. Stopped.");
    }
}

int32_t motor_position(void)
{
    uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
    position += (int16_t)(now - last_cnt);
    last_cnt = now;
    return position;
}

void motor_zero(void)
{
    taskENTER_CRITICAL();
    position = 0;
    last_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
    taskEXIT_CRITICAL();
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
    /* Check, clear and brake under one lock: masks the EXTI (priority 5),
     * so a fault can't land between the pin check and the brake. Post
     * after, no FreeRTOS calls in here. */
    taskENTER_CRITICAL();
    if (!((MOTOR_EN_GPIO_Port->ODR & MOTOR_EN_Pin) && (MOTOR_EN_GPIO_Port->IDR & MOTOR_EN_Pin))) {
        taskEXIT_CRITICAL();
        return false; /* line still low: keep the latch */
    }
    bool was_latched = fault_latched;
    fault_latched = false;
    if (was_latched) {
        bridge_brake();
    }
    taskEXIT_CRITICAL();

    if (was_latched) {
        event_post(EVENT_MOTOR_FAULT_CLEARED);
    }
    return true;
}

void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(MOTOR_EN_Pin) != 0U) {
        __HAL_GPIO_EXTI_CLEAR_IT(MOTOR_EN_Pin);
        if ((MOTOR_EN_GPIO_Port->ODR & MOTOR_EN_Pin) != 0U) {  /* not our own write */
            motor_fault_isr();
        }
    }
}

void motor_get_status(motor_status_t *status)
{
    if (!status) {
        return;
    }
    int32_t left = (int32_t)(run_deadline - xTaskGetTickCount());
    status->duty_permille = duty;
    status->position = position;
    status->current_ma = motor_current_ma();
    status->fault = fault_latched;
    status->fault_count = fault_count;
    status->run_active = run_active;
    status->ms_remaining = (run_active && left > 0) ? (uint32_t)left : 0;
}
