#include "my_hal.h"

#include <xprintf.h>
#include <uart_lib.h>
#include <mik32_hal.h>
#include <mik32_hal_pcc.h>
#include <mik32_hal_gpio.h>
#include <mik32_hal_dma.h>
#include "mik32_hal_scr1_timer.h"

#define CHECK_ERROR(status, msg) do { HAL_StatusTypeDef s = (status); \
        if (s != HAL_OK) { ret = s; xprintf(msg ", %" PRIu32 "\n", s); } \
    } while (0)

//Private
void trap_handler()
{
    if (UART_STDOUT_EPIC_CHECK())
    {
        if (UART_IsRxFifoFull(UART_STDOUT)) 
        {
            unsigned char rx = (unsigned char)UART_ReadByte(UART_STDOUT);
            cli_uart_rxcplt_callback(rx);
        }
    }
    else
    {
        interrupt_handler();
    }
    HAL_EPIC_Clear(0xFFFFFFFF);
}
__attribute__((__weak__)) void interrupt_handler(void)
{
    
}

static WDT_HandleTypeDef hwdt = {};
static SCR1_TIMER_HandleTypeDef hscr1 = {};

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
    PCC_OscInit.LSI32KCalibrationValue = 128;
    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;
    return HAL_PCC_Config(&PCC_OscInit);
}
static HAL_StatusTypeDef GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_PCC_GPIO_0_CLK_ENABLE(); //Needed, because GPIO lacks MspInit (desing decision)
    __HAL_PCC_GPIO_1_CLK_ENABLE();
    __HAL_PCC_GPIO_2_CLK_ENABLE();
    __HAL_PCC_GPIO_IRQ_CLK_ENABLE();

    //Init port 0
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    return HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct);

    //Init port ...
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
static HAL_StatusTypeDef WDT_Init()
{
    HAL_StatusTypeDef ret;

    hwdt.Instance = WDT;
    hwdt.Init.Clock = HAL_WDT_OSC32K;
    hwdt.Init.ReloadMs = 1000;
    ret = HAL_WDT_Init(&hwdt, WDT_TIMEOUT_DEFAULT);
    if (ret != HAL_OK) return ret;
    ret = HAL_WDT_Start(&hwdt, WDT_TIMEOUT_DEFAULT);
    if (ret != HAL_OK) return ret;
    return HAL_WDT_Refresh(&hwdt, WDT_TIMEOUT_DEFAULT);
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
    xdev_out(xputc);
    ret = UART_Init(UART_1, 32, control_1, 0, 0) ? //1Mbaud
        HAL_OK : HAL_ERROR;

    //Setup interrupt receiver buffer
    HAL_EPIC_MaskEdgeSet(HAL_EPIC_UART_1_MASK);

    return ret;
}
static void SCR1_Init(void)
{
    hscr1.Instance = SCR1_TIMER;
	hscr1.ClockSource = SCR1_TIMER_CLKSRC_INTERNAL; //внутренняя герцовка
	hscr1.Divider = 0; //без деления частоты
	HAL_SCR1_Timer_Init(&hscr1);
}

//Public
HAL_StatusTypeDef my_hal_init(void)
{
    HAL_StatusTypeDef ret = HAL_OK;

    HAL_Init();
    __HAL_PCC_PM_CLK_ENABLE();
    PCC_ConfigErrorsTypeDef clock_errors = SystemClock_Config();
    __HAL_PCC_EPIC_CLK_ENABLE();
    CHECK_ERROR(my_uart_init(), "UART init failed");
    xputs("UART init finished\n");
    xprintf("PCC init error codes: %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 "\n",
        clock_errors.FreqMonRef, clock_errors.SetOscSystem, clock_errors.RTCClock, clock_errors.CPURTCClock);
    CHECK_ERROR(WDT_Init(), "WDT init failed");
    xputs("WDT init finished\n");
    CHECK_ERROR(GPIO_Init(), "GPIO init failed");
    xputs("GPIO init finished\n");
    SCR1_Init();
    CHECK_ERROR(Timer32_Micros_Init(), "Timer init failed");
    xputs("Timer init finished\n");
    HAL_EPIC_Clear(0xFFFFFFFF);
    HAL_IRQ_EnableInterrupts();
    xputs("EPIC init finished\n");

    return ret;
}

void delay_us(uint32_t us)
{
    uint32_t start = TIMER_MICROS->VALUE;
    while ((TIMER_MICROS->VALUE - start) < us);
}
void delay_ms(uint32_t ms)
{
    HAL_DelayMs(&hscr1, ms);
}

void toggle_red_led(void)
{
    HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_10);
}

void toggle_green_led(void)
{
    HAL_GPIO_TogglePin(GPIO_0, GPIO_PIN_9);
}

void wdt_reset()
{
    HAL_WDT_Refresh(&hwdt, WDT_TIMEOUT_DEFAULT);
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
void xputc(char c)
{
    UART_WriteByte(UART_STDOUT, c);
	UART_WaitTransmission(UART_STDOUT);
}
