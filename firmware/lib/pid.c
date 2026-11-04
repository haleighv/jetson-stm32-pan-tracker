/*
 * pid.c
 *
 * Fixed-rate PID controller. See pid.h for usage notes.
 *
 * Author: Haleigh (haleighv)
 * Copyright (c) 2026 haleighv
 * SPDX-License-Identifier: MIT
 */
#include <math.h>

#include "pid.h"

/* Clamp a value to a specified range */
static double clamp(double value, double min, double max)
{
    if (value > max)
        return max;
    if (value < min)
        return min;
    return value;
}

/* Reject NaN and +/-inf. NaN compares false, so it would get past the min/max
 * checks below and end up in the output. */
static bool cfg_is_finite(const struct pid_config *cfg)
{
    return isfinite(cfg->kp) && isfinite(cfg->ki) && isfinite(cfg->kd) &&
           isfinite(cfg->i_term_min) && isfinite(cfg->i_term_max) &&
           isfinite(cfg->out_min) && isfinite(cfg->out_max);
}

pid_status_t pid_init(struct pid *pid, const struct pid_config *cfg)
{
    if (!pid || !cfg)
        return PID_ERR_NULL;

    /* Reset first so pid_tune() never re-clamps uninitialized state */
    pid_reset(pid);

    return pid_tune(pid, cfg);
}

pid_status_t pid_tune(struct pid *pid, const struct pid_config *cfg)
{
    if (!pid || !cfg)
        return PID_ERR_NULL;

    if (cfg->sample_period_us == 0)
        return PID_ERR_CFG;

    if (!cfg_is_finite(cfg))
        return PID_ERR_CFG;

    if (cfg->i_term_min >= cfg->i_term_max)
        return PID_ERR_CFG;

    if (cfg->out_min >= cfg->out_max)
        return PID_ERR_CFG;

    /* Copy the configuration */
    pid->cfg = *cfg;

    /* Period in seconds, computed once here instead of every pid_run() */
    pid->cfg.dt_s = cfg->sample_period_us / 1000000.0;

    /* Reclamp the integral term to the new limits */
    pid->st.i_term = clamp(pid->st.i_term, pid->cfg.i_term_min, pid->cfg.i_term_max);

    return PID_SUCCESS;
}

void pid_reset(struct pid *pid)
{
    pid->st.initialized = false;
    pid->st.last_input = 0.0;
    pid->st.i_term = 0.0;
    pid->st.last_output = 0.0;
}

double pid_run(struct pid *pid, double input, double target)
{
    /* Calculate the error */
    double error = target - input;

    /* First call after init/reset: no previous input yet, so P only */
    if (!pid->st.initialized)
    {
        pid->st.last_input = input;
        pid->st.initialized = true;
        pid->st.last_output = clamp(pid->cfg.kp * error,
                            pid->cfg.out_min, pid->cfg.out_max);
        return pid->st.last_output;
    }

    /* Calculate and clamp the integral term. ki is applied here so gain
     * changes don't rescale the accumulated sum. */
    pid->st.i_term += pid->cfg.ki * error * pid->cfg.dt_s;
    pid->st.i_term = clamp(pid->st.i_term, pid->cfg.i_term_min, pid->cfg.i_term_max);

    /* Calculate derivative on measurement */
    double input_derivative = (input - pid->st.last_input) / pid->cfg.dt_s;

    /* Calculate PID output. D is subtracted since it's on the input, not the error. */
    double output = (pid->cfg.kp * error)
                  + pid->st.i_term
                  - (pid->cfg.kd * input_derivative);

    /* Clamp output to actuator limits */
    output = clamp(output, pid->cfg.out_min, pid->cfg.out_max);

    /* Update state */
    pid->st.last_input = input;
    pid->st.last_output = output;

    return output;
}
