#include "my_hal.h"

#include <xprintf.h>
#include <uart_lib.h>
#include <mik32_hal.h>
#include <mik32_hal_pcc.h>
#include <mik32_hal_gpio.h>
#include <mik32_hal_dma.h>
#include <mik32_hal_scr1_timer.h>
#include <mik32_hal_spi.h>
#include "sys_command_line.h"

#define CHECK_ERROR(status, msg) do { HAL_StatusTypeDef s = (status); \
        if (s != HAL_OK) { ret = s; xprintf(msg ", %" PRIu32 "\n", s); } \
    } while (0)

//Private

static WDT_HandleTypeDef hwdt = {};
static SPI_HandleTypeDef hspi1 = {};
static TIMER32_HandleTypeDef htim_main_0 = {};
static TIMER32_CHANNEL_HandleTypeDef htim_main_0_ch_2 = {};
static TIMER32_CHANNEL_HandleTypeDef htim_main_0_ch_4 = {};
static DMA_InitTypeDef hdma;
static DMA_ChannelHandleTypeDef hdma_ch0;
static DMA_ChannelHandleTypeDef hdma_ch1;

void __attribute__(( optimize("O3") )) RAM_ATTR trap_handler(void)
{
    if (UART_STDOUT_EPIC_CHECK())
    {
        if ((UART_STDOUT->FLAGS & UART_FLAGS_RXNE_M) != 0) 
        {
            unsigned char rx = (unsigned char)(UART_STDOUT->RXDATA);
            cli_uart_rxcplt_callback(rx);
        }
    }
    else if (EPIC_CHECK_DMA())
    {
        HAL_SPI_CS_Enable(&hspi1, SPI_CS_0);
        HAL_DMA_ClearLocalIrq(&hdma);
    }
    EPIC->CLEAR = 0xFFFFFFFF;
}

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
static HAL_StatusTypeDef Timers_PWM_Init(void)
{
    HAL_StatusTypeDef ret = HAL_OK;

#if PWM_TOP > UINT16_MAX
    #error PWM_TOP has to be a 16-bit number
#endif
    if (PWM_TOP > UINT16_MAX) return HAL_ASSERTION_FAILED;

    /** MAIN 32-bit timers init */
    htim_main_0.Instance = TIMER32_1;
    htim_main_0.Top = PWM_TOP;
    htim_main_0.State = TIMER32_STATE_DISABLE;
    htim_main_0.Clock.Source = TIMER32_SOURCE_PRESCALER;
    htim_main_0.Clock.Prescaler = 0;
    htim_main_0.InterruptMask = 0;
    htim_main_0.CountMode = TIMER32_COUNTMODE_FORWARD;
    //xputs("Tim 0\n");
    CHECK_ERROR(HAL_Timer32_Init(&htim_main_0), "Main 0 PWM timer init failed");
    if (ret != HAL_OK) return ret;

    htim_main_0_ch_4.TimerInstance = htim_main_0.Instance;
    htim_main_0_ch_4.ChannelIndex = TIMER32_CHANNEL_3;
    htim_main_0_ch_4.PWM_Invert = TIMER32_CHANNEL_NON_INVERTED_PWM;
    htim_main_0_ch_4.Mode = TIMER32_CHANNEL_MODE_PWM;
    htim_main_0_ch_4.CaptureEdge = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
    htim_main_0_ch_4.OCR = 0;
    htim_main_0_ch_4.Noise = TIMER32_CHANNEL_FILTER_OFF;
    //xputs("Channel 0\n");
    CHECK_ERROR(HAL_Timer32_Channel_Init(&htim_main_0_ch_4), "Main 0 PWM channel init failed");
    if (ret != HAL_OK) return ret;

    htim_main_0_ch_2.TimerInstance = htim_main_0.Instance;
    htim_main_0_ch_2.ChannelIndex = TIMER32_CHANNEL_1;
    htim_main_0_ch_2.PWM_Invert = TIMER32_CHANNEL_NON_INVERTED_PWM;
    htim_main_0_ch_2.Mode = TIMER32_CHANNEL_MODE_PWM;
    htim_main_0_ch_2.CaptureEdge = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
    htim_main_0_ch_2.OCR = 0;
    htim_main_0_ch_2.Noise = TIMER32_CHANNEL_FILTER_OFF;
    //xputs("Channel 1\n");
    CHECK_ERROR(HAL_Timer32_Channel_Init(&htim_main_0_ch_2), "Main 1 PWM channel init failed");
    if (ret != HAL_OK) return ret;

    //xputs("Enable 0\n");
    CHECK_ERROR(HAL_Timer32_Channel_Enable(&htim_main_0_ch_4), "Main 0 PWM channel enable failed");
    if (ret != HAL_OK) return ret;
    //xputs("Enable 1\n");
    CHECK_ERROR(HAL_Timer32_Channel_Enable(&htim_main_0_ch_2), "Main 1 PWM channel enable failed");
    if (ret != HAL_OK) return ret;
    //xputs("Clear\n");
    HAL_Timer32_Value_Clear(&htim_main_0);

    //Start all timers
    HAL_Timer32_Start(&htim_main_0);

    return ret;
}
static HAL_StatusTypeDef WDT_Init()
{
    HAL_StatusTypeDef ret;

    hwdt.Instance = WDT;
    hwdt.Init.Clock = HAL_WDT_OSC32K;
    hwdt.Init.ReloadMs = 1000;
    CHECK_ERROR(HAL_WDT_Init(&hwdt, WDT_TIMEOUT_DEFAULT), "HAL_WDT_Init failed");
    HAL_DelayMs(1); //Required
    CHECK_ERROR(HAL_WDT_Start(&hwdt, WDT_TIMEOUT_DEFAULT), "Failed to start WDT");
    CHECK_ERROR(HAL_WDT_Refresh(&hwdt, WDT_TIMEOUT_DEFAULT), "Failed to refresh WDT");

    return ret;
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

    //Setup interrupt receiver buffer
    HAL_EPIC_MaskEdgeSet(UART_STDOUT_EPIC_MASK);

    return ret;
}
static void SCR1_Init(void)
{
    HAL_Time_SCR1TIM_Init();
}
static HAL_StatusTypeDef SPI_Init(void)
{
    hspi1.Instance = SPI_1;

    /* Режим SPI */
    hspi1.Init.SPI_Mode = HAL_SPI_MODE_MASTER;

    /* Настройки */
    hspi1.Init.CLKPhase = SPI_PHASE_ON;
    hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
    hspi1.Init.ThresholdTX = SPI_THRESHOLD_DEFAULT;

    /* Настройки для ведущего */
    hspi1.Init.BaudRateDiv = SPI_BAUDRATE_DIV32;
    hspi1.Init.Decoder = SPI_DECODER_NONE;
    hspi1.Init.ManualCS = SPI_MANUALCS_ON;
    hspi1.Init.ChipSelect = SPI_CS_0;

    HAL_StatusTypeDef ret = HAL_SPI_Init(&hspi1);
    HAL_SPI_SetDelayINIT(&hspi1, 255);
    HAL_SPI_SetDelayAFTER(&hspi1, 255);
    HAL_SPI_Enable(&hspi1);

    return ret;
}
static void DMA_CH0_Init(DMA_InitTypeDef *hdma)
{
    hdma_ch0.dma = hdma;

    /* Настройки канала */
    hdma_ch0.ChannelInit.Channel = DMA_CHANNEL_0;
    hdma_ch0.ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;

    hdma_ch0.ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;
    hdma_ch0.ChannelInit.ReadInc = DMA_CHANNEL_INC_ENABLE;
    hdma_ch0.ChannelInit.ReadSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно read_size */
    hdma_ch0.ChannelInit.ReadBurstSize = 0;                /* read_burst_size должно быть кратно read_size */
    hdma_ch0.ChannelInit.ReadRequest = DMA_CHANNEL_SPI_1_REQUEST;
    hdma_ch0.ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;

    hdma_ch0.ChannelInit.WriteMode = DMA_CHANNEL_MODE_PERIPHERY;
    hdma_ch0.ChannelInit.WriteInc = DMA_CHANNEL_INC_DISABLE;
    hdma_ch0.ChannelInit.WriteSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно write_size */
    hdma_ch0.ChannelInit.WriteBurstSize = 0;                /* write_burst_size должно быть кратно read_size */
    hdma_ch0.ChannelInit.WriteRequest = DMA_CHANNEL_SPI_1_REQUEST;
    hdma_ch0.ChannelInit.WriteAck = DMA_CHANNEL_ACK_DISABLE;
}

static void DMA_CH1_Init(DMA_InitTypeDef *hdma)
{
    hdma_ch1.dma = hdma;

    /* Настройки канала */
    hdma_ch1.ChannelInit.Channel = DMA_CHANNEL_1;
    hdma_ch1.ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;

    hdma_ch1.ChannelInit.ReadMode = DMA_CHANNEL_MODE_PERIPHERY;
    hdma_ch1.ChannelInit.ReadInc = DMA_CHANNEL_INC_DISABLE;
    hdma_ch1.ChannelInit.ReadSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно read_size */
    hdma_ch1.ChannelInit.ReadBurstSize = 0;                /* read_burst_size должно быть кратно read_size */
    hdma_ch1.ChannelInit.ReadRequest = DMA_CHANNEL_SPI_1_REQUEST;
    hdma_ch1.ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;

    hdma_ch1.ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;
    hdma_ch1.ChannelInit.WriteInc = DMA_CHANNEL_INC_ENABLE;
    hdma_ch1.ChannelInit.WriteSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно write_size */
    hdma_ch1.ChannelInit.WriteBurstSize = 0;                /* write_burst_size должно быть кратно read_size */
    hdma_ch1.ChannelInit.WriteRequest = DMA_CHANNEL_SPI_1_REQUEST;
    hdma_ch1.ChannelInit.WriteAck = DMA_CHANNEL_ACK_DISABLE;

    HAL_DMA_LocalIRQEnable(&hdma_ch1, DMA_IRQ_ENABLE);
}
static HAL_StatusTypeDef DMA_Init(void)
{
    /* Настройки DMA */
    hdma.Instance = DMA_CONFIG;
    hdma.CurrentValue = DMA_CURRENT_VALUE_ENABLE;
    HAL_StatusTypeDef ret = HAL_DMA_Init(&hdma);
    /* Инициализация канала */
    DMA_CH0_Init(&hdma);
    DMA_CH1_Init(&hdma);
    HAL_EPIC_MaskLevelSet(HAL_EPIC_DMA_MASK);
    return ret;
}

//Public
HAL_StatusTypeDef my_hal_init(void)
{
    HAL_StatusTypeDef ret = HAL_OK;

    write_csr(mtvec, RAM_BASE_ADDRESS);
    HAL_Init();
    __HAL_PCC_PM_CLK_ENABLE();
    PCC_ConfigErrorsTypeDef clock_errors = SystemClock_Config();
    __HAL_PCC_EPIC_CLK_ENABLE();
    CHECK_ERROR(my_uart_init(), "UART init failed");
    xputs("UART init finished\n");
    xprintf("PCC init error codes: %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 "\n",
        clock_errors.FreqMonRef, clock_errors.SetOscSystem, clock_errors.RTCClock, clock_errors.CPURTCClock);
#if ENABLE_WDT
    CHECK_ERROR(WDT_Init(), "WDT init failed");
    xputs("WDT init finished\n");
#endif
    CHECK_ERROR(GPIO_Init(), "GPIO init failed");
    xputs("GPIO init finished\n");
    SCR1_Init();
    CHECK_ERROR(Timer32_Micros_Init(), "Timer Micros init failed");
    CHECK_ERROR(Timers_PWM_Init(), "Timer PWM init failed");
    xputs("Timer init finished\n");
    CHECK_ERROR(SPI_Init(), "SPI init failed");
    xputs("SPI init fininshed\n");
    CHECK_ERROR(DMA_Init(), "DMA init failed");
    xputs("DMA init finished\n");
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
    HAL_DelayMs(ms);
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
#if ENABLE_WDT
    HAL_WDT_Refresh(&hwdt, WDT_TIMEOUT_DEFAULT);
#endif
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

HAL_StatusTypeDef spi_dummy_transmit(void)
{
#define SPI_DUMMY_SZ 8
    static uint8_t tx[SPI_DUMMY_SZ] = { 0x00, 0x0B, 0x0A, 0x0D, 0x0F, 0x00, 0x00, 0x0D };
    static uint8_t rx[SPI_DUMMY_SZ];

    HAL_SPI_CS_Disable(&hspi1);
    HAL_DMA_Start(&hdma_ch0, tx, (void*)&hspi1.Instance->TXDATA, SPI_DUMMY_SZ - 1);
    HAL_DMA_Start(&hdma_ch1, (void *)&hspi1.Instance->RXDATA, rx, SPI_DUMMY_SZ - 1);
    HAL_DMA_Wait(&hdma_ch0, DMA_TIMEOUT_DEFAULT);
    HAL_DMA_Wait(&hdma_ch1, DMA_TIMEOUT_DEFAULT);

    return HAL_OK;
}

HAL_StatusTypeDef set_pwm_duty(motor_t ch, uint16_t duty)
{
    switch (ch)
    {
    case MOTOR_MAIN_0:
        return HAL_Timer32_Channel_OCR_Set(&htim_main_0_ch_4, duty);
    case MOTOR_MAIN_1:
        return HAL_Timer32_Channel_OCR_Set(&htim_main_0_ch_2, duty);
    case MOTOR_AUX:
        return HAL_OK;
    
    default:
        return HAL_ASSERTION_FAILED;
    }
}
