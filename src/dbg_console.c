#include "sys_command_line.h"

#include "my_hal.h"
#include "main.h"
#include "nvs.h"

#include <mik32_hal_eeprom.h>
#include <stdio.h>
#include <xprintf.h>

/***
 * Command declarations
 */

uint8_t cli_reset(int argc, char *argv[]);
uint8_t dbg_device_info(int argc, char** argv);
uint8_t dbg_enable_jtag(int argc, char** argv);
uint8_t dbg_report(int argc, char** argv);

uint8_t dbg_hw_report(int argc, char** argv);
uint8_t dbg_coproc_report(int argc, char** argv);

uint8_t dbg_nvs_save(int argc, char** argv);
uint8_t dbg_nvs_load(int argc, char** argv);
uint8_t dbg_nvs_reset(int argc, char** argv);
uint8_t dbg_nvs_report(int argc, char** argv);
uint8_t dbg_nvs_test(int argc, char** argv);
uint8_t dbg_nvs_dump(int argc, char** argv);
uint8_t dbg_nvs_print_errors(int argc, char** argv);

uint8_t dbg_set_kp(int argc, char** argv);
uint8_t dbg_set_ki(int argc, char** argv);
uint8_t dbg_set_target_jog_speed(int argc, char** argv);
uint8_t dbg_set_encoder_coef(int argc, char** argv);

uint8_t dbg_measure_adc_channel_directly(int argc, char** argv);
uint8_t dbg_measure_adc_channels(int argc, char** argv);
uint8_t dbg_motor_run(int argc, char** argv);
uint8_t dbg_stop(int argc, char** argv);
uint8_t dbg_motion_debug(int argc, char** argv);

/***
 * Private API
 */

int try_parse_float(char* arg, float* val)
{
    if (sscanf(arg, "%f", val) != 1) return 2;
    return 0;
}

/***
 * Public API
 */

void my_dbg_console_init()
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    CLI_INIT();

    CLI_ADD_CMD("reset", "Reboot MCU", cli_reset);
    CLI_ADD_CMD("info", "Get device info", dbg_device_info);
    CLI_ADD_CMD("dbg_report", "Report debugging info", dbg_report);

    CLI_ADD_CMD("nvs_save", "Save current non-volatile data into EEPROM", dbg_nvs_save);
    CLI_ADD_CMD("nvs_load", "Load non-volatile data from EEPROM", dbg_nvs_load);
    CLI_ADD_CMD("nvs_reset", "Reset NVS (sets NVS partiton version to 0 [invalid], doesn't actually erase the EEPROM)",
        dbg_nvs_reset);
    CLI_ADD_CMD("nvs_report", "Report NVS contents in human-readable format", dbg_nvs_report);
    CLI_ADD_CMD("nvs_test", "Test NVS read-write and CRC calculation", dbg_nvs_test);
    CLI_ADD_CMD("nvs_dump", "Hex dump of the RAM cache", dbg_nvs_dump);
    CLI_ADD_CMD("err_store_report", "Print the contents of error memory", dbg_nvs_print_errors);
}

/***
 * COMMAND definitions
 */

/**
  * @brief  MCU reboot
  * @param  para addr. & length
  * @retval True means OK
  */
uint8_t cli_reset(int argc, char *argv[])
{
	if(argc > 1){
		xprintf("Command \"%s\" takes no argument.", argv[0]);NL1();
		return 1;
	}

#if !ENABLE_WDT
    HAL_StatusTypeDef ret;
    if ((ret = wdt_start()) != HAL_OK) return ret;
#endif
	NL1();xprintf("[END]: System Rebooting");NL1();
	while (1);
	return 0;
}
uint8_t dbg_device_info(int argc, char** argv)
{
    xputs(MY_FIRMWARE_INFO_STR "\n");
    return 0;
}
uint8_t dbg_report(int argc, char** argv)
{
    register void *sp asm ("sp");
    xprintf("SP = %08" PRIXPTR "\n"
        "MTVAL = %08" PRIX32 "\n"
        "MEPC = %08" PRIX32 "\n"
        "MCAUSE = %08" PRIX32 "\n"
        "MIP = %08" PRIX32 "\n"
        "EECON = %08" PRIX32 "\n"
        "EEADJ = %08" PRIX32 "\n"
        "EESTA = %08" PRIX32 "\n",
        sp, read_csr(mtval), read_csr(mepc), read_csr(mcause), read_csr(mip),
        EEPROM_REGS->EECON, EEPROM_REGS->EEADJ, EEPROM_REGS->EESTA);
    uint32_t eeprom_crc;
    if (my_nvs_get_whole_eeprom_crc32(&eeprom_crc) == HAL_OK)
    {
        xprintf("EEPROM CRC = %08" PRIX32 "\n", eeprom_crc);
    }
    else
    {
        xputs("Failed to calculate EEPROM CRC!\n");
    }
    return 0;
}

uint8_t dbg_nvs_save(int argc, char** argv)
{
    return my_nvs_save();
}
uint8_t dbg_nvs_load(int argc, char** argv)
{
    return my_nvs_load();
}
uint8_t dbg_nvs_reset(int argc, char** argv)
{
    return my_nvs_reset();
}
uint8_t dbg_nvs_report(int argc, char** argv)
{
    //Common parts
    xprintf("** EEPROM Error stats: %" PRIu32 " **\n"
        "Casement: %" PRIu32 "\n"
        "Motion timeout: %" PRIu32 "\n",
        get_eeprom_error_stats(),
        (uint32_t)(nvs_storage_handle->casement_config),
        nvs_storage_handle->motion_timeout);
    //Main motor settings
    xprintf("Main motor:\n"
        "\tJog target 0 = %f\n"
        "\tJog target 1 = %f\n"
        "\tAccel 0 = %f\n"
        "\tAccel 1 = %f\n"
        "\tEncoder 0 m/cts = %f\n"
        "\tEncoder 1 m/cts = %f\n",
        nvs_storage_handle->jog_target_speed_0,
        nvs_storage_handle->jog_target_speed_1,
        nvs_storage_handle->acceleration_target_0,
        nvs_storage_handle->acceleration_target_1,
        nvs_storage_handle->encoder_counts_to_meters_0,
        nvs_storage_handle->encoder_counts_to_meters_1);
    for (uint32_t i = 0; i < MAIN_MOTOR_COUNT; i++)
    {
        const pid_tunings_t* instance = &(nvs_storage_handle->tunings_0) + i;
        xprintf("\tPID tunings %" PRIu32 ":\n"
            "\t\tkI = %f\n"
            "\t\tkP = %f\n"
            "\t\tMin pwr = %f\n"
            "\t\tBrk scale = %f\n",
            i,
            instance->kI,
            instance->kP,
            instance->min_power,
            instance->brake_scaling);
    }
    for (uint32_t i = 0; i < MAIN_MOTOR_COUNT; i++)
    {
        xprintf("\tMain %" PRIu32 " dir: %" PRIu32 "\n"
            "\tMain %" PRIu32 " I_lim = %f\n",
            i, nvs_storage_handle->main_motor_dir[i],
            i, nvs_storage_handle->main_current_limit[i]);
    }
    xprintf("\tTarget open 0 = %f\n"
        "\tTarget closed 0 = %f\n"
        "\tTarget p.open 0 = %f\n"
        "\tTarget open 1 = %f\n"
        "\tTarget closed 1 = %f\n"
        "\tTarget p.open 1 = %f\n"
        "\tHard brk time = %f\n"
        "\tPos precision = %f\n",
        nvs_storage_handle->target_open_distance_0,
        nvs_storage_handle->target_closed_distance_0,
        nvs_storage_handle->target_partial_open_distance_0,
        nvs_storage_handle->target_open_distance_1,
        nvs_storage_handle->target_closed_distance_1,
        nvs_storage_handle->target_partial_open_distance_1,
        nvs_storage_handle->hard_brake_time,
        nvs_storage_handle->position_precision);
    //AUX motor settings
    for (uint32_t i = 0; i < AUX_MOTOR_COUNT; i++)
    {
        xprintf("AUX motor %" PRIu32 ":\n"
            "\tPwr = %f\n"
            "\tDir: %" PRIu32 "\n"
            "\tI_lim = %f\n",
            i,
            nvs_storage_handle->aux_motor_power[i],
            (uint32_t)(nvs_storage_handle->aux_motor_dir[i]),
            nvs_storage_handle->aux_current_limit[i]);
    }
    //Seal
    xprintf("Seal:\n"
        "\tEn: %" PRIu32 "\n"
        "\tVent target P = %f\n"
        "\tPump max P = %f\n"
        "\tPump min P = %f\n",
        (uint32_t)(nvs_storage_handle->seal_enabled),
        nvs_storage_handle->vent_target_pressure,
        nvs_storage_handle->pump_max_pressure,
        nvs_storage_handle->pump_min_pressure);
    //Steps
    xprintf("Steps:\n"
        "\tEn: %" PRIu32 "\n"
        "\tDual: %" PRIu32 "\n",
        (uint32_t)(nvs_storage_handle->steps_enabled),
        (uint32_t)(nvs_storage_handle->steps_dual));
    //Coprocessor
    xprintf("Coproc out inv: %06b\n", nvs_storage_handle->coproc_gpio_out_invert);
    //Misc
    xprintf("NVS CRC = 0x%08" PRIX32 "\n"
        "NVS Ver = %" PRIu32 "\n",
        nvs_storage_handle->crc32,
        my_nvs_get_version());
    return 0;
}
uint8_t dbg_nvs_test(int argc, char** argv)
{
    return my_nvs_test();
}
uint8_t dbg_nvs_dump(int argc, char** argv)
{
    my_nvs_hexdump();
    return 0;
}
uint8_t dbg_nvs_print_errors(int argc, char** argv)
{
    my_nvs_print_errors();
    return 0;
}