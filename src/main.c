#include <xprintf.h>

#include "my_hal.h"
#include "sys_command_line.h"

soft_timer led_timer = { .interval = 1000000 };
soft_timer cli_timer = { .interval = 5000 };
soft_timer pwm_timer = { .interval = 1000 };

int main()
{ 
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    if (__builtin_expect(init_result != HAL_OK, 0)) while (1);

    while (1)
    {
        wdt_reset();
        if (check_soft_timer(&pwm_timer))
        {
            typedef enum
            {
                PWM_STATE_nA_nB,
                PWM_STATE_A_nB,
                PWM_STATE_A_B,
                PWM_STATE_nA_B
            } pwm_engine_states;

            static bool pwm1 = false;
            static bool pwm2 = false;
            static pwm_engine_states pwm_state = PWM_STATE_nA_nB;

            switch (pwm_state)
            {
            case PWM_STATE_nA_nB:
                toggle_soft_pwm1();
                pwm_state = PWM_STATE_A_nB;
                break;
            case PWM_STATE_A_nB:
                toggle_soft_pwm2();
                pwm_state = PWM_STATE_A_B;
                break;
            case PWM_STATE_A_B:
                toggle_soft_pwm1();
                pwm_state = PWM_STATE_nA_B;
                break;
            case PWM_STATE_nA_B:
                toggle_soft_pwm2();
                pwm_state = PWM_STATE_nA_nB;
                break;
            default:
                break;
            }
        }
        if (check_soft_timer(&led_timer))
        {
            //static float dummy = 0;

            toggle_green_led();
            //xprintf("Tick %.2f\n", dummy);
            //dummy += 1.5f;
        }
        if (check_soft_timer(&cli_timer))
        {
            cli_run();
        }
    }
    __unreachable();
}
