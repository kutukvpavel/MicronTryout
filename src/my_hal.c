#include "my_hal.h"

#include <xprintf.h>
#include <uart_lib.h>
#include <mik32_hal.h>
#include <mik32_hal_pcc.h>
#include <mik32_hal_gpio.h>
#include <mik32_hal_adc.h>
#include <assert.h>

#define CHECK_ERROR(status, msg) do { HAL_StatusTypeDef s = (status); \
        if (s != HAL_OK) { ret = s; xprintf(msg ", %" PRIu32 "\n", s); } \
    } while (0)

//Private

static ADC_HandleTypeDef hadc = {};
static uint8_t adc_channels_in_use[] = {
    ADC_CHANNEL0,
    ADC_CHANNEL1,
    ADC_CHANNEL2,
    ADC_CHANNEL3,
    ADC_CHANNEL4,
    /*ADC_CHANNEL5,
    ADC_CHANNEL6,*/
    ADC_CHANNEL7
};

static void UART_putc(char c)
{
    UART_WriteByte(UART_STDOUT, c);
	UART_WaitTransmission(UART_STDOUT);
}
static PCC_ConfigErrorsTypeDef SystemClock_Config(void)
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
    PCC_OscInit.LSI32KCalibrationValue = 8;
    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;
    return HAL_PCC_Config(&PCC_OscInit);
}
static HAL_StatusTypeDef GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_StatusTypeDef ret;

    __HAL_PCC_GPIO_0_CLK_ENABLE(); //Needed, because GPIO lacks MspInit (desing decision)
    __HAL_PCC_GPIO_1_CLK_ENABLE();
    __HAL_PCC_GPIO_2_CLK_ENABLE();
    __HAL_PCC_GPIO_IRQ_CLK_ENABLE();

    //Init port 0. Initialize all possible pins to analog
    GPIO_InitStruct.Pin = 
#if !USE_JTAG
        GPIO_PIN_13 | /*GPIO_PIN_11 |*/
#endif
        /*GPIO_PIN_9 |*/ GPIO_PIN_7 | GPIO_PIN_4 | GPIO_PIN_2;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    if ((ret = HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct)) != HAL_OK) return ret;

    //Init port 1. Initialize all possible pins to analog
    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    ret = HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);

    return ret;
}
static HAL_StatusTypeDef Timer32_Micros_Init(void)
{
    TIMER32_HandleTypeDef htimer32;

    htimer32.Instance = TIMER_MICROS;
    htimer32.Top = 0xFFFFFFFF;
    htimer32.State = TIMER32_STATE_DISABLE;
    htimer32.Clock.Source = TIMER32_SOURCE_PRESCALER;
    htimer32.Clock.Prescaler = 32; //1 MHz clock for 1uS per tick timer
    htimer32.InterruptMask = 0;
    htimer32.CountMode = TIMER32_COUNTMODE_FORWARD;
    
    HAL_StatusTypeDef res = HAL_Timer32_Init(&htimer32);
    if (res == HAL_OK) 
    {
        HAL_Timer32_Value_Clear(&htimer32);
        HAL_Timer32_Start(&htimer32);
    }

    return res;
}
static HAL_StatusTypeDef my_uart_init()
{
    HAL_StatusTypeDef ret;

    //Setup USART    
    const uint32_t control_1 =
        UART_CONTROL1_TE_M | //Enable TX
        UART_CONTROL1_RE_M | //Enable RX
        UART_CONTROL1_PCE_M | //Enable parity check
        UART_CONTROL1_PS_M | //Odd parity
        UART_CONTROL1_M_9BIT_M | //9 bit packet
        UART_CONTROL1_RXNEIE_M; //Received interrupt
    xdev_out(UART_putc);
    ret = UART_Init(UART_STDOUT, 32, control_1, 0, 0) ? //1Mbaud
        HAL_OK : HAL_ERROR;

    return ret;
}
static HAL_StatusTypeDef ADC_Init(void)
{
    hadc.Instance = ANALOG_REG;
    hadc.Init.Sel = ADC_CHANNEL0;
    hadc.Init.EXTRef = ADC_EXTREF_OFF;    /* Выбор источника опорного напряжения: «1» - внешний; «0» - встроенный */
    hadc.Init.EXTClb = ADC_EXTCLB_ADCREF; /* Выбор источника внешнего опорного напряжения: «1» - внешний вывод; «0» - настраиваемый ОИН */
    HAL_ADC_Init(&hadc);
    HAL_ADC_ContinuousEnable(&hadc);
    
    return HAL_OK;
}

//Public
const uint8_t* const adc_channels_in_use_ptr = adc_channels_in_use;

HAL_StatusTypeDef my_hal_init(void)
{
    static_assert(sizeof(adc_channels_in_use) == TOTAL_ADC_CHANNELS_IN_USE, "");

    HAL_StatusTypeDef ret = HAL_OK;

    HAL_Init();
    __HAL_PCC_PM_CLK_ENABLE();
    PCC_ConfigErrorsTypeDef clock_errors = SystemClock_Config();
    __HAL_PCC_EPIC_CLK_ENABLE();
    CHECK_ERROR(my_uart_init(), "UART init failed");
    xputs("UART init finished\n");
    xprintf("PCC init error codes: %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 "\n",
        clock_errors.FreqMonRef, clock_errors.SetOscSystem, clock_errors.RTCClock, clock_errors.CPURTCClock);
    CHECK_ERROR(Timer32_Micros_Init(), "Timer Micros init failed");
    xputs("TIM32 micros init finished\n");
    CHECK_ERROR(GPIO_Init(), "GPIO init failed");
    xputs("GPIO init finished\n");
    CHECK_ERROR(ADC_Init(), "ADC init failed");
    xputs("ADC init finished\n");

    xputs(FW_VERSION "\n");

    return ret;
}

void delay_us(uint32_t us)
{
    uint32_t start = TIMER_MICROS->VALUE;
    while ((TIMER_MICROS->VALUE - start) < us);
}
void set_adc_channel(uint8_t channel)
{
    hadc.Init.Sel = channel;
    HAL_ADC_ChannelSet(&hadc);
}
float get_adc_voltage(void)
{
    return HAL_ADC_GetValue(&hadc) * (1.2f / 4095.0f);
}


bool check_soft_timer(soft_timer* t)
{
    bool ret = get_time_past(t->last_time) > t->interval;
    if (ret) t->last_time = get_micros();
    return ret;
}
uint32_t get_time_past(uint32_t from)
{
    return get_micros() - from;
}
uint32_t get_micros(void)
{
    return TIMER_MICROS->VALUE;
}

//Weak overrides