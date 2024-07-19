#pragma once

#include <stdlib.h>
#include <inttypes.h>
#include <stddef.h>

#include <mik32_hal.h>
#include <mik32_hal_timer32.h>
#include <mik32_hal_irq.h>
#include <mik32_hal_wdt.h>
#include <uart.h>

#define TIMER_MICROS TIMER32_2
#define UART_STDOUT UART_1
#define UART_STDOUT_EPIC_CHECK() EPIC_CHECK_UART_1()
#define UART_BUF_SIZE 128

#define _BV(bit) (1u << (bit))

//Public API

struct _soft_timer
{
    uint32_t interval; //us
    uint32_t last_time;
} typedef soft_timer;

HAL_StatusTypeDef my_hal_init(void);
void delay_us(uint32_t us);
void toggle_red_led(void);
void toggle_green_led(void);

extern inline uint32_t get_micros(void);
extern inline uint32_t get_time_past(uint32_t from);
extern inline bool check_soft_timer(soft_timer* t);
extern inline void wdt_reset();
extern inline void delay_ms(uint32_t ms);

//ISRs

extern void interrupt_handler(void);
extern void cli_uart_rxcplt_callback(unsigned char rx);