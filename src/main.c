#include <xprintf.h>
#include <stdlib.h>

#include "my_hal.h"

soft_timer cli_timer = { .interval = 1000000 };

float adc_channel_values[TOTAL_ADC_CHANNELS_IN_USE] = { 0 };

int main()
{
    HAL_StatusTypeDef init_result = my_hal_init();
    xprintf("Init result: %" PRIu32 "\n", init_result);
    if (__builtin_expect(init_result != HAL_OK, 0)) while (1);

    while (1)
    {
#define START() start = get_micros()
#define STOP() stop = get_micros()

        int32_t i1 = rand();
        int32_t i2 = rand();
        float f1 = rand() * 0.01f;
        float f2 = rand() * 0.01f;
        uint32_t start;
        uint32_t stop;
        float f;
        int32_t i;
        
        START();
        f = f1 / f2;
        STOP();

        xprintf("Div t = %" PRIu32 ", f = %f\n", stop - start, f);

        START();
        f = f1 * f2;
        STOP();

        xprintf("Mul t = %" PRIu32 ", f = %f\n", stop - start, f);
        
        START();
        i = i1 / i2;
        STOP();

        xprintf("Div t = %" PRIu32 ", i = %" PRId32 "\n", stop - start, i);

        START();
        i = i1 * i2;
        STOP();

        xprintf("Mul t = %" PRIu32 ", i = %" PRId32 "\n", stop - start, i);

        delay_us(1000000);
    }
    __unreachable();
}
