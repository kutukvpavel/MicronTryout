#include <xprintf.h>

#include "my_hal.h"
#include "sys_command_line.h"

soft_timer led_timer = { .interval = 1000000 };
soft_timer cli_timer = { .interval = 5000 };
soft_timer spi_timer = { .interval = 12000 };

int main()
{ 
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    if (__builtin_expect(init_result != HAL_OK, 0)) while (1);
    cli_init();

    while (1)
    {
        wdt_reset();
        if (check_soft_timer(&spi_timer))
        {
            static uint16_t duty0 = 0;
            static uint16_t duty1 = PWM_TOP;

            spi_dummy_transmit();
            set_pwm_duty(MOTOR_MAIN_0, duty0);
            set_pwm_duty(MOTOR_MAIN_1, duty1);
            duty0 += 10;
            if (duty0 >= PWM_TOP) duty0 = 0;
            duty1 -= 10;
            if (duty1 <= 0) duty1 = PWM_TOP;
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
