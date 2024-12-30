#pragma once

#include <inttypes.h>
#include <stddef.h>

#include <mik32_hal.h>
#include <mik32_hal_timer32.h>
#include <mik32_hal_irq.h>
#include <uart.h>

#define FW_VERSION "2"

#define USE_JTAG 1 //Be careful with pins 0.13 and 0.11
#define TOTAL_ADC_CHANNELS_IN_USE 6

#define REFERENCE_VOLTAGE 3.0f

#define TIMER_MICROS TIMER32_2
#define UART_STDOUT UART_1
#define UART_STDOUT_EPIC_MASK EPIC_UART_1_INDEX
#define UART_STDOUT_EPIC_CHECK() EPIC_CHECK_UART_1()
#define UART_BUF_SIZE 128

#define _BV(bit) (1u << (bit))
#define RAM_ATTR __attribute__( ( noinline, section(".ram_text") ) )

//Public API

struct _soft_timer
{
    uint32_t interval; //us
    uint32_t last_time;
} typedef soft_timer;

extern const uint8_t* const adc_channels_in_use_ptr;

HAL_StatusTypeDef my_hal_init(void);

void delay_us(uint32_t us);
void set_adc_channel(uint8_t channel);
float get_adc_voltage(void);
void set_dac(uint16_t v);

extern inline uint32_t get_micros(void);
extern inline uint32_t get_time_past(uint32_t from);
extern inline bool check_soft_timer(soft_timer* t);
