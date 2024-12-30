#include <xprintf.h>

#include "my_hal.h"

#define CONVERSIONS_TO_DISCARD 0

soft_timer adc_timer = { .interval = 500 };
soft_timer cli_timer = { .interval = 1000000 };

float adc_channel_values[TOTAL_ADC_CHANNELS_IN_USE] = { 0 };

int main()
{
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    if (__builtin_expect(init_result != HAL_OK, 0)) while (1);

    set_adc_channel(adc_channels_in_use_ptr[0]);

    while (1)
    {
        if (check_soft_timer(&adc_timer))
        {
            //static size_t current_adc_channel_index = 0;
            static uint16_t current_dac_voltage = 0;

            /*adc_channel_values[current_adc_channel_index] = get_adc_voltage();
            if (++current_adc_channel_index >= TOTAL_ADC_CHANNELS_IN_USE) current_adc_channel_index = 0;
            set_adc_channel(adc_channels_in_use_ptr[current_adc_channel_index]);*/

            current_dac_voltage += 2;
            if (current_dac_voltage > 0x0FFF) current_dac_voltage = 0;
            set_dac(current_dac_voltage);
        }
        /*if (check_soft_timer(&cli_timer))
        {
            for (size_t i = 0; i < TOTAL_ADC_CHANNELS_IN_USE; i++)
            {
                xprintf("CH #%" PRIu32 ": %.3f V; ",
                    (uint32_t)(adc_channels_in_use_ptr[i]), adc_channel_values[i]);   
            }
            xputc('\r');
        }*/
    }
    __unreachable();
}
