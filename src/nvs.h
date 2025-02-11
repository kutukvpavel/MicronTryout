#pragma once

#include "my_pid.h"
#include "my_hal.h"
#include "my_err.h"

#include <mik32_hal.h>

#include <stddef.h>
#include <stdint.h>

#define MY_NVS_ERR_TEST_FAILED 0xFE
#define MY_NVS_ERR_CRC_FAILED 0xFD
#define MY_NVS_ERROR_STORAGE_LEN 16u

typedef enum
{
    CONFIG_SINGLE_CASEMENT_SINGLE_MOTOR,
    CONFIG_SINGLE_CASEMENT_DUAL_MOTOR,
    CONFIG_DUAL_CASEMENT
} casement_configurations_t;

typedef struct
{
    casement_configurations_t casement_config;
    uint32_t motion_timeout; //uS
    uint32_t homing_timeout; //uS
    //Main motor motion params
    float homing_speed_0;
    float homing_speed_1;
    float jog_target_speed_0;
    float jog_target_speed_1;
    float acceleration_target_0;
    float acceleration_target_1;
    float encoder_counts_to_meters_0;
    float encoder_counts_to_meters_1;
    pid_tunings_t tunings_0;
    pid_tunings_t tunings_1;
    direction_t main_motor_dir[MAIN_MOTOR_COUNT];
    direction_t encoder_dir[MAIN_MOTOR_COUNT];
    float main_current_limit[MAIN_MOTOR_COUNT];
    float main_power_limit[MAIN_MOTOR_COUNT];
    float target_open_distance_0;
    float target_closed_distance_0;
    float target_partial_open_distance_0;
    float target_open_distance_1;
    float target_closed_distance_1;
    float target_partial_open_distance_1;
    float hard_brake_time;
    float position_precision;
    float velocity_precision;
    //AUX motor motion params
    float aux_motor_power[AUX_MOTOR_COUNT];
    direction_t aux_motor_dir[AUX_MOTOR_COUNT];
    float aux_current_limit[AUX_MOTOR_COUNT];
    //Seal params
    bool seal_enabled;
    float vent_target_pressure;
    float pump_max_pressure;
    float pump_min_pressure;
    //Steps params
    bool steps_enabled;
    bool steps_dual;
    //Coprocessor settings
    uint32_t coproc_gpio_out_invert;
    //Consistency check
    uint32_t crc32;
} __attribute__(( __aligned__(4) )) nvs_storage_t;

typedef struct
{
    uint16_t code;
    uint16_t arg;
} __attribute__(( __aligned__(4) )) nvs_err_data_t;
typedef struct
{
    uint32_t present;
    uint32_t index;
    uint32_t count;
    nvs_err_data_t error_data[MY_NVS_ERROR_STORAGE_LEN];
    uint32_t crc32;
} __attribute__(( __aligned__(4) )) nvs_error_storage_t;

HAL_StatusTypeDef my_nvs_initialize(nvs_storage_t** return_ptr);
HAL_StatusTypeDef my_nvs_save(void);
HAL_StatusTypeDef my_nvs_reset(void);
HAL_StatusTypeDef my_nvs_load(void);
HAL_StatusTypeDef my_nvs_test(void);
uint32_t my_nvs_get_version(void);
void my_nvs_hexdump(void);
HAL_StatusTypeDef my_nvs_get_whole_eeprom_crc32(uint32_t* crc);

const nvs_error_storage_t* my_nvs_err_storage_init(void);
void my_nvs_save_error(my_err_t err, uint16_t arg);
void my_nvs_print_errors(void);