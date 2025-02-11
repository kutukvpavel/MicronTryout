#pragma once

#define MY_PID_DELTA_TIME 2000 //us

typedef struct
{
    float kI;
    float kP;
    float min_power;
    float brake_scaling;
} pid_tunings_t;

typedef struct
{
    const pid_tunings_t* tunings;
    float integrator;
    float last_setpoint;
    float last_output;
} pid_instance_t;

void my_pid_initialize_coefficients(pid_instance_t* instance, const pid_tunings_t* tunings);

void my_pid_reset_integrator(pid_instance_t* instance);
float my_pid_calculate(pid_instance_t* instance, float feedback, float setpoint);
