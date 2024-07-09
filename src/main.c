#include "mik32_hal_pcc.h"
#include "mik32_hal_gpio.h"

#include "uart_lib.h"
#include "xprintf.h"

void SystemClock_Config();
void GPIO_Init();

const volatile char dummy_string[16000] = {};

int main()
{
    GPIO_Init();
    HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_10);
    
    if (UART_Init(UART_1, 3333, UART_CONTROL1_TE_M | UART_CONTROL1_M_8BIT_M, 0, 0))
    {
        HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_10);
        HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_9);
    }
    xdev_out(xputc);
    xputs("Start\n");

    SystemClock_Config();

    while (1)
    {
        HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_9);
        HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_10);
        xputs("Puff\n");
        for (volatile uint32_t i = 0; i < 3200000; i++);
    }
}

void SystemClock_Config(void)
{
    PCC_InitTypeDef PCC_OscInit = {0};

    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;
    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;
    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;
    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;
    PCC_OscInit.AHBDivider = 0;
    PCC_OscInit.APBMDivider = 0;
    PCC_OscInit.APBPDivider = 0;
    PCC_OscInit.HSI32MCalibrationValue = 128;
    PCC_OscInit.LSI32KCalibrationValue = 128;
    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;
    HAL_PCC_Config(&PCC_OscInit);
}

void GPIO_Init()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /*__HAL_PCC_GPIO_0_CLK_ENABLE();
    __HAL_PCC_GPIO_1_CLK_ENABLE();
    __HAL_PCC_GPIO_2_CLK_ENABLE();
    __HAL_PCC_GPIO_IRQ_CLK_ENABLE();*/

    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct);
}

void xputc(char c)
{
    UART_WriteByte(UART_1, c);
	UART_WaitTransmission(UART_1);
}