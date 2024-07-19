#include <xprintf.h>

#include "my_hal.h"
#include "sys_command_line.h"

soft_timer led_timer = { .interval = 1000000 };
soft_timer cli_timer = { .interval = 5000 };

int main()
{ 
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    cli_init();

    while (1)
    {
        wdt_reset();
        if (check_soft_timer(&led_timer))
        {
            toggle_green_led();
        }
        if (check_soft_timer(&cli_timer))
        {
            cli_run();
        }
    }
}
