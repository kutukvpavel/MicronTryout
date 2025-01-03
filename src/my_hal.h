#pragma once

#include <inttypes.h>
#include <stddef.h>

#include <mik32_hal.h>
#include <mik32_hal_timer32.h>
#include <mik32_hal_irq.h>
#include <mik32_hal_wdt.h>
#include <uart.h>

#define ENABLE_WDT 1

#define PWM_TOP 16000

#define TIMER_MICROS TIMER32_2
#define UART_STDOUT UART_1
#define UART_STDOUT_EPIC_MASK EPIC_UART_1_INDEX
#define UART_STDOUT_EPIC_CHECK() EPIC_CHECK_UART_1()
#define UART_BUF_SIZE 128

#define HAL_ASSERTION_FAILED 0x04

#define _BV(bit) (1u << (bit))
#define RAM_ATTR __attribute__( ( noinline, section(".ram_text") ) )

//Public API

enum
{
    MOTOR_MAIN_0,
    MOTOR_MAIN_1,
    MOTOR_AUX
} typedef motor_t;
struct _soft_timer
{
    uint32_t interval; //us
    uint32_t last_time;
} typedef soft_timer;

HAL_StatusTypeDef my_hal_init(void);
void delay_us(uint32_t us);
void toggle_red_led(void);
void toggle_green_led(void);
HAL_StatusTypeDef spi_dummy_transmit(void);
HAL_StatusTypeDef set_pwm_duty(motor_t ch, uint16_t duty);

extern inline uint32_t get_micros(void);
extern inline uint32_t get_time_past(uint32_t from);
extern inline bool check_soft_timer(soft_timer* t);
extern inline void wdt_reset();
extern inline void delay_ms(uint32_t ms);
