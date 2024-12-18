#include <xprintf.h>

#include "my_hal.h"

soft_timer adc_timer = { .interval = 10000 };
soft_timer cli_timer = { .interval = 1000000 };

float adc_channel_values[TOTAL_ADC_CHANNELS_IN_USE] = { 0 };

/**
 * 
    Channel Port    Pin
    3	    0	    2
    4	    0	    4
    5	    0	    7

 * 
 */

int main()
{
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    if (__builtin_expect(init_result != HAL_OK, 0)) while (1);

    start_adc_conversion(adc_channels_in_use_ptr[0]);

    while (1)
    {
        if (check_soft_timer(&adc_timer))
        {
            static size_t current_adc_channel_index = 0;
            static bool first_conversion_discarded = false;

            if (get_adc_conversion_finished())
            {
                if (first_conversion_discarded)
                {
                    adc_channel_values[current_adc_channel_index] = get_adc_voltage();
                    if (++current_adc_channel_index >= TOTAL_ADC_CHANNELS_IN_USE) current_adc_channel_index = 0;
                    start_adc_conversion(adc_channels_in_use_ptr[current_adc_channel_index]);
                    first_conversion_discarded = false;
                }
                else
                {
                    start_adc_conversion(adc_channels_in_use_ptr[current_adc_channel_index]);
                    first_conversion_discarded = true;
                }
            }
        }
        if (check_soft_timer(&cli_timer))
        {
            for (size_t i = 0; i < TOTAL_ADC_CHANNELS_IN_USE; i++)
            {
                xprintf("CH #%" PRIu32 ": %.3f V; ",
                    (uint32_t)(adc_channels_in_use_ptr[i]), adc_channel_values[i]);   
            }
            xputc('\r');
        }
    }
    __unreachable();
}
