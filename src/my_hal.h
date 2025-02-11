#pragma once

#include <inttypes.h>
#include <stddef.h>

#include <mik32_hal.h>
#include <mik32_hal_timer32.h>
#include <mik32_hal_irq.h>
#include <mik32_hal_wdt.h>
#include <mik32_hal_scr1_timer.h>
#include <uart.h>

#define MY_FIRMWARE_INFO_STR "fw_eeprom-v0.1"

#define ENABLE_WDT 0

#define PWM_TOP 16000
#define UART_STDOUT UART_1
#define UART_STDOUT_EPIC_MASK EPIC_UART_1_INDEX
#define UART_STDOUT_EPIC_CHECK() EPIC_CHECK_UART_1()
#define UART_BUF_SIZE 128
#define MAIN_MOTOR_COUNT 2
#define AUX_MOTOR_COUNT 4
#define TOTAL_MOTOR_COUNT (MAIN_MOTOR_COUNT + AUX_MOTOR_COUNT)
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
typedef enum
{
    MOTOR_CW = 0,
    MOTOR_CCW
} direction_t;
struct _soft_timer
{
    uint32_t interval; //us
    uint32_t last_time;
} typedef soft_timer;
struct _soft_timer_32
{
    uint32_t interval; //us
    uint32_t last_time;
} typedef soft_timer_32;

HAL_StatusTypeDef my_hal_init(void);
void delay_us(uint64_t us);
void toggle_red_led(void);
void toggle_green_led(void);
HAL_StatusTypeDef spi_dummy_transmit(void);
HAL_StatusTypeDef set_pwm_duty(motor_t ch, uint16_t duty);
uint32_t get_eeprom_error_stats(void);

inline uint64_t get_micros(void)
{
    return __HAL_SCR1_TIMER_GET_TIME();
}
inline uint32_t get_micros_32(void)
{
    return SCR1_TIMER->MTIME;
}
inline uint32_t get_time_past_32(uint32_t from)
{
    return get_micros_32() - from;
}
inline uint64_t get_time_past(uint64_t from)
{
    return get_micros() - from;
}
bool check_soft_timer(soft_timer* t);
bool check_soft_timer_32(soft_timer_32* t);
void reset_micros(void);
void __attribute__(( optimize("O3") )) wdt_reset(void);
#if !ENABLE_WDT
HAL_StatusTypeDef wdt_start(void);
#endif