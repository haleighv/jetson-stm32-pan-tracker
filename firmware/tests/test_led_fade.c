/*
 * test_led_fade.c
 *
 * Host unit tests for led_fade.c (Unity).
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include "unity.h"
#include "led_fade.h"

#define PERIOD  2000
#define STEPS   10

void setUp(void) {}
void tearDown(void) {}

/* Starts and ends each cycle off */
static void test_off_at_start_and_end(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, led_fade_duty(0, PERIOD, STEPS));
    TEST_ASSERT_EQUAL_UINT8(0, led_fade_duty(PERIOD, PERIOD, STEPS));   /* wraps to 0 */
}

/* Fully on at the halfway point */
static void test_full_at_half(void)
{
    TEST_ASSERT_EQUAL_UINT8(STEPS, led_fade_duty(PERIOD / 2, PERIOD, STEPS));
}

/* Squared curve: a quarter of the way is 1/4 brightness, not 1/2 */
static void test_quarter_is_squared(void)
{
    /* x = 0.5 of half -> 0.25 * 10 = 2.5 -> rounds to 3 */
    TEST_ASSERT_EQUAL_UINT8(3, led_fade_duty(PERIOD / 4, PERIOD, STEPS));
}

/* Falling half mirrors the rising half */
static void test_symmetric(void)
{
    for (uint32_t p = 0; p <= PERIOD / 2; p += 50) {
        TEST_ASSERT_EQUAL_UINT8(led_fade_duty(p, PERIOD, STEPS),
                                led_fade_duty(PERIOD - p, PERIOD, STEPS));
    }
}

/* Never gets dimmer while rising */
static void test_rising_is_monotonic(void)
{
    uint8_t prev = 0;
    for (uint32_t p = 0; p <= PERIOD / 2; p++) {
        uint8_t d = led_fade_duty(p, PERIOD, STEPS);
        TEST_ASSERT_TRUE(d >= prev);
        TEST_ASSERT_TRUE(d <= STEPS);
        prev = d;
    }
}

/* Bad inputs don't divide by zero */
static void test_degenerate_inputs(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, led_fade_duty(5, 0, STEPS));
    TEST_ASSERT_EQUAL_UINT8(0, led_fade_duty(5, 1, STEPS));
    TEST_ASSERT_EQUAL_UINT8(0, led_fade_duty(5, PERIOD, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_at_start_and_end);
    RUN_TEST(test_full_at_half);
    RUN_TEST(test_quarter_is_squared);
    RUN_TEST(test_symmetric);
    RUN_TEST(test_rising_is_monotonic);
    RUN_TEST(test_degenerate_inputs);
    return UNITY_END();
}
