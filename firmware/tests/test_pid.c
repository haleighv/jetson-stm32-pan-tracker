/*
 * test_pid.c
 *
 * Host unit tests for pid.c (Unity).
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include <math.h>

#include "unity.h"
#include "pid.h"

/* Shared fixture: a fresh controller and a known-good config before each test */
static struct pid pid;
static struct pid_config cfg;

void setUp(void)
{
    pid = (struct pid){0};
    cfg = (struct pid_config){
        .sample_period_us = 1000,   /* 1 kHz */
        .kp = 2.0,
        .ki = 0.0,
        .kd = 0.0,
        .i_term_min = -1.0,
        .i_term_max = 1.0,
        .out_min = -1.0,
        .out_max = 1.0,
    };
}

void tearDown(void) {}

/* First call after init is P only: output = kp * (target - input) */
static void test_p_only_first_call(void)
{
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    double out = pid_run(&pid, 0.0, 0.25);    /* error = 0.25 */

    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, out);
}

/* Closed loop on a first-order plant y' = (u - y) / tau settles at the setpoint */
static void test_converges_on_first_order_plant(void)
{
    cfg.ki = 20.0;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    const double dt = 0.001;
    const double tau = 0.05;
    const double setpoint = 0.5;
    double y = 0.0;

    for (int k = 0; k < 2000; k++)            /* 2 s */
    {
        double u = pid_run(&pid, y, setpoint);
        y += (dt / tau) * (u - y);
    }

    TEST_ASSERT_DOUBLE_WITHIN(0.01 * setpoint, setpoint, y);   /* within 1% */
}

/* NaN compares false, so it needs its own check in pid_tune() */
static void test_rejects_nan_config(void)
{
    cfg.out_max = NAN;

    TEST_ASSERT_EQUAL_INT(PID_ERR_CFG, pid_init(&pid, &cfg));
}

/* output after pid_reset() matches a fresh controller */
static void test_reset(void)
{
    cfg.ki = 20.0;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    const double dt = 0.001;
    const double tau = 0.05;
    const double setpoint = 0.5;
    double y = 0.0;

    for (int k = 0; k < 100; k++)
    {
        double u = pid_run(&pid, y, setpoint);
        y += (dt / tau) * (u - y);
    }

    struct pid pid_clean;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid_clean, &cfg));

    pid_reset(&pid);

    TEST_ASSERT_EQUAL_DOUBLE(pid_clean.st.last_output, pid.st.last_output);
    TEST_ASSERT_EQUAL_DOUBLE(pid_clean.st.i_term, pid.st.i_term);
    TEST_ASSERT_EQUAL_DOUBLE(pid_clean.st.last_input, pid.st.last_input);
    TEST_ASSERT_EQUAL_INT(pid_clean.st.initialized, pid.st.initialized);

    double out_reset = pid_run(&pid, 0.2, setpoint);
    double out_clean = pid_run(&pid_clean, 0.2, setpoint);
    TEST_ASSERT_EQUAL_DOUBLE(out_clean, out_reset);
}

/* output never leaves [out_min, out_max], plant settles at the limit */
static void test_output_limits(void)
{
    cfg.ki = 20.0;
    cfg.out_max = 0.5;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    const double dt = 0.001;
    const double tau = 0.05;
    const double setpoint = 1.0;
    double y = 0.0;

    for (int k = 0; k < 2000; k++)            /* 2 s */
    {
        double u = pid_run(&pid, y, setpoint);
        y += (dt / tau) * (u - y);
        TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(cfg.out_max, u);
        TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(cfg.out_min, u);
    }

    TEST_ASSERT_DOUBLE_WITHIN(0.01, cfg.out_max, y);   /* can't pass the limit */
}

/* saturate for 2 s, then reverse the error: output should leave the limit
 * on the first step. Without the i_term clamp, i_term would reach ~40 and
 * hold the output at out_max for a long time. */
static void test_anti_windup(void)
{
    cfg.ki = 20.0;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    for (int k = 0; k < 2000; k++)            /* 2 s, input stuck at 0 */
    {
        double u = pid_run(&pid, 0.0, 1.0);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, cfg.out_max, u);
        TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(cfg.i_term_max, pid.st.i_term);
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, cfg.i_term_max, pid.st.i_term);

    double u = pid_run(&pid, 0.0, -0.25);     /* error flips sign */

    TEST_ASSERT_LESS_THAN_DOUBLE(cfg.out_max, u);
}

/* setpoint step with constant input: D contributes 0 (derivative on measurement).
 * With D on error this step would add kd * 0.25 / dt = 25 and saturate. */
static void test_no_derivative_kick(void)
{
    cfg.kd = 0.1;
    TEST_ASSERT_EQUAL_INT(PID_SUCCESS, pid_init(&pid, &cfg));

    double out = pid_run(&pid, 0.0, 0.0);     /* first call, P only */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, out);

    out = pid_run(&pid, 0.0, 0.25);           /* setpoint step, input held */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, out);   /* kp * 0.25 only */

    /* D still acts on the measurement: input moves 0.001 in 1 ms -> 1.0/s */
    out = pid_run(&pid, 0.001, 0.25);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0 * 0.249 - 0.1 * 1.0, out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_p_only_first_call);
    RUN_TEST(test_converges_on_first_order_plant);
    RUN_TEST(test_rejects_nan_config);
    RUN_TEST(test_reset);
    RUN_TEST(test_output_limits);
    RUN_TEST(test_anti_windup);
    RUN_TEST(test_no_derivative_kick);
    return UNITY_END();
}
