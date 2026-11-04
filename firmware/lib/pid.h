/*
 * pid.h
 *
 * Fixed-rate PID controller. Used by the STM32 control loop and built on the
 * host for unit tests. No dynamic memory, no hardware access.
 *
 * Notes:
 * - Caller runs pid_run() once per sample period (timer ISR). No clock reads.
 * - Derivative is on the measurement, so setpoint steps don't kick.
 * - Integral term and output are clamped to the configured limits.
 * - pid_tune() keeps the integral term so gain changes don't bump the output.
 * - Not thread-safe. One context per instance.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#ifndef PID_H
#define PID_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Return codes for pid_init() and pid_tune() */
typedef enum {
    PID_SUCCESS = 0,
    PID_ERR = -1,       /* general error */
    PID_ERR_NULL = -2,  /* NULL argument */
    PID_ERR_CFG = -3    /* bad config: period of 0, min >= max, NaN/inf */
} pid_status_t;

/* Config, copied in by pid_init() and pid_tune() */
struct pid_config {
    /* Timing */
    uint32_t sample_period_us;  /* pid_run() call period, must be > 0 */
    double dt_s;                /* period in seconds; derived in init/tune */
    /* Gains */
    double kp;
    double ki;                  /* per second */
    double kd;                  /* seconds */
    /* Limits */
    double i_term_min;          /* anti-windup clamp */
    double i_term_max;
    double out_min;             /* actuator units */
    double out_max;
};

/* State, cleared by pid_init() and pid_reset(), kept by pid_tune() */
struct pid_state {
    /* State */
    bool initialized;           /* false until first pid_run() after init/reset */
    double last_input;          /* previous measurement, for the derivative */
    double i_term;              /* integral sum, ki already applied */
    double last_output;
};

/* One instance per loop (position, velocity) */
struct pid {
    struct pid_config cfg;
    struct pid_state st;
};

/* Validate and copy cfg, clear state. */
pid_status_t pid_init(struct pid *pid, const struct pid_config *cfg);

/* Validate and copy a new cfg. Keeps state, re-clamps i_term to new limits. */
pid_status_t pid_tune(struct pid *pid, const struct pid_config *cfg);

/* Clear state, keep cfg. Use on arm, after a fault, or on mode change.
 * pid must not be NULL. */
void pid_reset(struct pid *pid);

/*
 * One controller step, once per sample period. input and target in the same
 * units. Returns the clamped output. First call after init/reset is P only.
 * pid must be initialized; not checked here since this runs in the ISR.
 */
double pid_run(struct pid *pid, double input, double target);

#ifdef __cplusplus
}
#endif

#endif /* PID_H */
