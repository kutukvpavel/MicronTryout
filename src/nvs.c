#include "nvs.h"

#include <xprintf.h>
#include <mik32_hal_eeprom.h>
#include <string.h>
#include <assert.h>

#define EEPROM_PAGE_START 32
#define EEPROM_PAGE_WORDS 32
#define EEPROM_PAGE_COUNT 64
#define EEPROM_ERROR_STORAGE_PAGE (16) //Offset from EEPROM_PAGE_START
#define EEPROM_OP_TIMEOUT 100000
#define MY_EEPROM_ERR_VERSION_MISMATCH 0xFF
#define GET_PAGE_ADDR(x) ((EEPROM_PAGE_START + (x)) * EEPROM_PAGE_WORDS * 4)
#define GET_STORAGE_CRC(buf) xcrc32((uint8_t*)(buf), offsetof(nvs_storage_t, crc32))
#define GET_ERROR_STORAGE_CRC(buf) xcrc32((uint8_t*)(buf), offsetof(nvs_error_storage_t, crc32))

#define MY_STORAGE_VERSION 3 //Metadata occupies the first page

#define DEFAULT_JOG_SPEED 0.2 //m/s
#define DEFAULT_ACCELERATION 0.05 //m/s^2
#define DEFAULT_ENCODER_MULTIPLIER 1E-4f //m/count
#define DEFAULT_AUX_POWER 0.5f //0..1
#define DEFAULT_CURRENT_LIMIT 2.0f //amps
#define DEFAULT_MAIN_POWER_LIMIT 0.9f //fraction
#define DEFAULT_KP 5.0f
#define DEFAULT_KI 0.1f
#define DEFAULT_MIN_POWER 0.1f
#define DEFAULT_BRAKE_SCALING 2.0f
#define DEFAULT_OPEN_DISTANCE 1.0f //m
#define DEFAULT_CLOSED_DISTANCE 0 //m
#define DEFAULT_PARTIAL_OPEN_DISTANCE 0.5f //m
#define DEFAULT_HOMING_SPEED 0.02 //m/s

static nvs_storage_t storage = {
    .casement_config = CONFIG_SINGLE_CASEMENT_SINGLE_MOTOR,
    .motion_timeout = 30000000, //uS
    .homing_timeout = 90000000, //uS
    .homing_speed_0 = DEFAULT_HOMING_SPEED,
    .homing_speed_1 = DEFAULT_HOMING_SPEED,
    .jog_target_speed_0 = DEFAULT_JOG_SPEED,
    .jog_target_speed_1 = DEFAULT_JOG_SPEED,
    .acceleration_target_0 = DEFAULT_ACCELERATION,
    .acceleration_target_1 = DEFAULT_ACCELERATION,
    .encoder_counts_to_meters_0 = DEFAULT_ENCODER_MULTIPLIER,
    .encoder_counts_to_meters_1 = DEFAULT_ENCODER_MULTIPLIER,
    .tunings_0 = { .kP = DEFAULT_KP, .kI = DEFAULT_KI, .min_power = DEFAULT_MIN_POWER, .brake_scaling = DEFAULT_BRAKE_SCALING },
    .tunings_1 = { .kP = DEFAULT_KP, .kI = DEFAULT_KI, .min_power = DEFAULT_MIN_POWER, .brake_scaling = DEFAULT_BRAKE_SCALING },
    .main_motor_dir = {
        MOTOR_CW,
        MOTOR_CW
    },
    .encoder_dir = {
        MOTOR_CW,
        MOTOR_CW
    },
    .main_current_limit = { 
        DEFAULT_CURRENT_LIMIT,
        DEFAULT_CURRENT_LIMIT
    },
    .main_power_limit = {
        DEFAULT_MAIN_POWER_LIMIT,
        DEFAULT_MAIN_POWER_LIMIT
    },
    .target_open_distance_0 = DEFAULT_OPEN_DISTANCE, //m
    .target_closed_distance_0 = DEFAULT_CLOSED_DISTANCE, //m
    .target_partial_open_distance_0 = DEFAULT_PARTIAL_OPEN_DISTANCE, //m
    .target_open_distance_1 = DEFAULT_OPEN_DISTANCE, //m
    .target_closed_distance_1 = DEFAULT_CLOSED_DISTANCE, //m
    .target_partial_open_distance_1 = DEFAULT_PARTIAL_OPEN_DISTANCE, //m
    .hard_brake_time = 2.0f, //s
    .position_precision = 0.005f, //m
    .velocity_precision = 0.002f, //m/s
    .aux_motor_power = { 
        DEFAULT_AUX_POWER,
        DEFAULT_AUX_POWER,
        DEFAULT_AUX_POWER,
        DEFAULT_AUX_POWER
    },
    .aux_motor_dir = {
        MOTOR_CW,
        MOTOR_CW,
        MOTOR_CW,
        MOTOR_CW
    },
    .aux_current_limit = {
        DEFAULT_CURRENT_LIMIT,
        DEFAULT_CURRENT_LIMIT,
        DEFAULT_CURRENT_LIMIT,
        DEFAULT_CURRENT_LIMIT
    },
    .seal_enabled = false,
    .vent_target_pressure = 0.05f, //atm
    .pump_max_pressure = 1.3f, //atm
    .pump_min_pressure = 1.1f, //atm
    .steps_enabled = false,
    .steps_dual = false,
    .coproc_gpio_out_invert = 0
};
static uint32_t storage_version = 0;
static HAL_EEPROM_HandleTypeDef heeprom = {
    .Instance = EEPROM_REGS,
    .Mode = HAL_EEPROM_MODE_THREE_STAGE,
    .ErrorCorrection = HAL_EEPROM_ECC_ENABLE,
    .EnableInterrupt = HAL_EEPROM_SERR_DISABLE
};
static const size_t storage_pages = sizeof(storage) / (sizeof(uint32_t) * EEPROM_PAGE_WORDS);
static const size_t storage_remainder_words = ((sizeof(storage) + (sizeof(uint32_t) - 1)) / sizeof(uint32_t)) % EEPROM_PAGE_WORDS;

/**
 * PRIVATE API
 */

static HAL_StatusTypeDef read(uint32_t* dest)
{
    HAL_StatusTypeDef ret = HAL_OK;
    uint32_t remainder_buffer[EEPROM_PAGE_WORDS];

    for (size_t i = 0; i < storage_pages; i++)
    {
        ret = HAL_EEPROM_Read(&heeprom, GET_PAGE_ADDR(i + 1), dest + i * EEPROM_PAGE_WORDS, EEPROM_PAGE_WORDS, EEPROM_OP_TIMEOUT);
    }
    if (ret != HAL_OK) return ret;
    if (storage_remainder_words > 0)
    {
        ret = HAL_EEPROM_Read(&heeprom, GET_PAGE_ADDR(storage_pages + 1), remainder_buffer, EEPROM_PAGE_WORDS, EEPROM_OP_TIMEOUT);
        for (size_t i = 0; i < storage_remainder_words; i++)
        {
            (dest + storage_pages * EEPROM_PAGE_WORDS)[i] = remainder_buffer[i];
        }
    }
    return ret;
}
static HAL_StatusTypeDef write(const uint32_t* src)
{
    HAL_StatusTypeDef ret = HAL_OK;
    uint32_t remainder_buffer[EEPROM_PAGE_WORDS] = { 0 };

    for (size_t i = 0; i < storage_pages; i++)
    {
        ret = HAL_EEPROM_Write(&heeprom, GET_PAGE_ADDR(i + 1), (uint32_t*)(src + i * EEPROM_PAGE_WORDS), 
            EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
    }
    if (ret != HAL_OK) return ret;
    if (storage_remainder_words > 0)
    {
        for (size_t i = 0; i < storage_remainder_words; i++)
        {
            remainder_buffer[i] = (src + storage_pages * EEPROM_PAGE_WORDS)[i];
        }
        ret = HAL_EEPROM_Write(&heeprom, GET_PAGE_ADDR(storage_pages + 1), remainder_buffer, 
            EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
    }
    return ret;
}

/* This table was generated by the following program.

   #include <stdio.h>

   int
   main ()
   {
     unsigned int i, j;
     unsigned int c;
     int table[256];

     for (i = 0; i < 256; i++)
       {
	 for (c = i << 24, j = 8; j > 0; --j)
	   c = c & 0x80000000 ? (c << 1) ^ 0x04c11db7 : (c << 1);
	 table[i] = c;
       }

     printf ("static const unsigned int crc32_table[] =\n{\n");
     for (i = 0; i < 256; i += 4)
       {
	 printf ("  0x%08x, 0x%08x, 0x%08x, 0x%08x",
		 table[i + 0], table[i + 1], table[i + 2], table[i + 3]);
	 if (i + 4 < 256)
	   putchar (',');
	 putchar ('\n');
       }
     printf ("};\n");
     return 0;
   }

   For more information on CRC, see, e.g.,
   http://www.ross.net/crc/download/crc_v3.txt. 
*/
static const uint32_t crc32_table[] =
{
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
  0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
  0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
  0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
  0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
  0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
  0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
  0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
  0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
  0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
  0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
  0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
  0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
  0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
  0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
  0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
  0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
  0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
  0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
  0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
  0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
  0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
  0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
  0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
  0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
  0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
  0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
  0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
  0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
  0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
  0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
  0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
  0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
  0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
  0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
  0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
  0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
  0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
  0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
  0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
  0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
  0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
  0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};
/*

@deftypefn Extension {unsigned int} crc32 (const unsigned char *@var{buf}, @
  int @var{len}, unsigned int @var{init})

Compute the 32-bit CRC of @var{buf} which has length @var{len}.  The
starting value is @var{init}; this may be used to compute the CRC of
data split across multiple buffers by passing the return value of each
call as the @var{init} parameter of the next.

This is used by the @command{gdb} remote protocol for the @samp{qCRC}
command.  In order to get the same results as gdb for a block of data,
you must pass the first CRC parameter as @code{0xffffffff}.

This CRC can be specified as:

  Width  : 32
  Poly   : 0x04c11db7
  Init   : parameter, typically 0xffffffff
  RefIn  : false
  RefOut : false
  XorOut : 0

This differs from the "standard" CRC-32 algorithm in that the values
are not reflected, and there is no final XOR value.  These differences
make it easy to compose the values of multiple blocks.

@end deftypefn

*/
static uint32_t xcrc32_step(uint32_t prev_crc, uint8_t next_byte)
{
    return (prev_crc << 8u) ^ crc32_table[((prev_crc >> 24u) ^ next_byte) & 255u];
}
static uint32_t xcrc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    while (len--)
    {
        crc = xcrc32_step(crc, *buf++);
    }
    return crc;
}

/**
 * PUBLIC API
 */

HAL_StatusTypeDef __noinline my_nvs_initialize(nvs_storage_t** return_ptr)
{
    static_assert(GET_PAGE_ADDR(storage_pages + 1) < GET_PAGE_ADDR(EEPROM_ERROR_STORAGE_PAGE));

    HAL_EEPROM_Init(&heeprom);
    HAL_EEPROM_CalculateTimings(&heeprom, OSC_SYSTEM_VALUE);
    HAL_StatusTypeDef ret;
    if ((ret = my_nvs_load()) != HAL_OK)
    {
        xprintf("NVS load error: %" PRIX32 "\n", ret);
        //Return default if version mismatch is detected
        switch ((uint32_t)ret)
        {
        case MY_EEPROM_ERR_VERSION_MISMATCH:
        case MY_NVS_ERR_CRC_FAILED:
            *return_ptr = &storage; //Do not fail all the way if this 
            break;
        default:
            *return_ptr = NULL;
            break;
        }
    }
    else *return_ptr = &storage;
    return ret;
}

HAL_StatusTypeDef my_nvs_save(void)
{
    static const uint32_t version[EEPROM_PAGE_WORDS] = { MY_STORAGE_VERSION };

    HAL_StatusTypeDef ret = my_nvs_reset(); //Erase required pages
    if (ret != HAL_OK) return ret;
    //Write version
    ret = HAL_EEPROM_Write(&heeprom, GET_PAGE_ADDR(0), (uint32_t*)version, EEPROM_PAGE_WORDS,
        HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
    if (ret != HAL_OK) return ret;
    //Write data
    storage.crc32 = GET_STORAGE_CRC(&storage);
    return write((uint32_t*)(&storage));
}
HAL_StatusTypeDef my_nvs_reset(void)
{
    HAL_StatusTypeDef ret;
    //Erase metadata
    ret = HAL_EEPROM_Erase(&heeprom, GET_PAGE_ADDR(0), EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
    if (ret != HAL_OK) return ret;
    //Erase data
    for (size_t i = 0; i < (storage_pages + 1u); i++)
    {
        ret = HAL_EEPROM_Erase(&heeprom, GET_PAGE_ADDR(i + 1), EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
        if (ret != HAL_OK) return ret;
    }
    return HAL_OK;
}
HAL_StatusTypeDef my_nvs_load(void)
{
    HAL_StatusTypeDef ret;
    nvs_storage_t comparison_buffer = {};

    //Check version
    ret = HAL_EEPROM_Read(&heeprom, GET_PAGE_ADDR(0), &storage_version, 1, EEPROM_OP_TIMEOUT);
    if (ret != HAL_OK) return ret;
    xprintf("NVS ver = %" PRIu32 "\n", storage_version);
    if (storage_version != MY_STORAGE_VERSION) return MY_EEPROM_ERR_VERSION_MISMATCH;

    //Read data
    ret = read((uint32_t*)(&comparison_buffer));
    if (ret != HAL_OK) return ret;
    uint32_t crc_expect = comparison_buffer.crc32;
    comparison_buffer.crc32 = GET_STORAGE_CRC(&comparison_buffer);
    if (crc_expect != comparison_buffer.crc32) ret = MY_NVS_ERR_CRC_FAILED;
    else
    {
        storage = comparison_buffer; //This will copy
    }

    return ret;
}
HAL_StatusTypeDef __attribute__(( optimize("O0"), __noinline__ )) my_nvs_test(void)
{
    nvs_storage_t comparison_buffer = {};
    HAL_StatusTypeDef ret = HAL_OK;

    xprintf("Testing NVS:\nStorage size: whole pages = %" PRIu32 ", remainder words = %" PRIu32 "\n",
        storage_pages, storage_remainder_words);

    xputs("Calc CRC...\n");
    storage.crc32 = GET_STORAGE_CRC(&storage);

    xputs("Erase EEPROM...\n");
    for (size_t i = 0; i < (storage_pages + 1); i++)
    {
        ret = HAL_EEPROM_Erase(&heeprom, GET_PAGE_ADDR(i + 1), EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
        if (ret != HAL_OK) return ret;
    }

    xputs("Write...\n");
    ret = write((uint32_t*)(&storage));
    if (ret != HAL_OK) return ret;

    xputs("Read...\n");
    ret = read((uint32_t*)(&comparison_buffer));
    if (ret != HAL_OK) return ret;

    xputs("Compare contents...\n"
        "#\tW\tR\n");
    for (uint32_t i = 0; i < sizeof(nvs_storage_t); i++)
    {
        uint8_t w = ((uint8_t*)(&storage))[i];
        uint8_t r = ((uint8_t*)(&comparison_buffer))[i];
        xprintf("%3" PRIu32 "\t%02" PRIX32 "\t%02" PRIX32, i, w, r);
        if (w != r) xputs("\t<---");
        xputc('\n');
    }
    
    xputs("Compare CRC...\n");
    uint32_t crc_comp = GET_STORAGE_CRC(&comparison_buffer);
    if (crc_comp != storage.crc32)
    {
        xprintf("CRC doesn't match: calc = 0x%08" PRIX32 ", stored = 0x%08" PRIX32 "\n",
            crc_comp, storage.crc32);
        ret = MY_NVS_ERR_CRC_FAILED;
    }

    xputs("Reset NVS.\n");
    my_nvs_reset();
    return ret;
}
uint32_t my_nvs_get_version(void)
{
    return storage_version;
}
void my_nvs_hexdump(void)
{
    const uint8_t* ptr = (uint8_t*)(&storage);
    xputs("NVS Dump:\n");
    for (size_t i = 0; i < sizeof(storage); i++)
    {
        xprintf("%02" PRIX32 "\n", (uint32_t)(ptr[i]));   
    }
}
HAL_StatusTypeDef my_nvs_get_whole_eeprom_crc32(uint32_t* crc)
{
    uint32_t buffer[EEPROM_PAGE_WORDS];
    HAL_StatusTypeDef ret;

    *crc = 0xFFFFFFFF;
    for (size_t i = 0; i < EEPROM_PAGE_COUNT; i++)
    {
        if ((ret = HAL_EEPROM_Read(&heeprom, i * EEPROM_PAGE_WORDS * 4, buffer, EEPROM_PAGE_WORDS, EEPROM_OP_TIMEOUT))
            != HAL_OK)
            return ret;
        for (size_t j = 0; j < (EEPROM_PAGE_WORDS * 4); j++)
        {
            *crc = xcrc32_step(*crc, ((uint8_t*)buffer)[j]);
        }
    }
    return HAL_OK;
}

/**
 * ERROR STORAGE
 */

static nvs_error_storage_t error_storage = {
    .present = 0,
    .index = 0,
    .count = 0,
    .error_data = { },
    .crc32 = 0
};
static const size_t error_storage_words = (sizeof(error_storage) + (sizeof(uint32_t) - 1)) / sizeof(uint32_t);

const nvs_error_storage_t* __noinline my_nvs_err_storage_init(void)
{
    static_assert(sizeof(nvs_error_storage_t) < (EEPROM_PAGE_WORDS * sizeof(uint32_t)));
    static_assert(EEPROM_ERROR_STORAGE_PAGE < (EEPROM_PAGE_COUNT - EEPROM_PAGE_START));

    HAL_StatusTypeDef ret;
    uint32_t buffer[EEPROM_PAGE_WORDS];

    ret = HAL_EEPROM_Read(&heeprom, GET_PAGE_ADDR(EEPROM_ERROR_STORAGE_PAGE), buffer, EEPROM_PAGE_WORDS, EEPROM_OP_TIMEOUT);
    if (ret == HAL_OK)
    {
        uint32_t crc_expected = ((nvs_error_storage_t*)buffer)->crc32;
        uint32_t crc_calculated = GET_ERROR_STORAGE_CRC(buffer);
        if (crc_expected != crc_calculated)
        {
            xprintf("Err storage CRC doesn't match: calc = 0x%08" PRIX32 ", stored = 0x%08" PRIX32 ". Resetting.\n",
                crc_calculated, crc_expected);
            my_nvs_save_error(MY_ERR_ERR_STORAGE_CRC, 0);
        }
        else
        {
            for (size_t i = 0; i < error_storage_words; i++)
            {
                ((uint32_t *)(&error_storage))[i] = buffer[i];
            }
        }
    }
    else
        xputs("Failed to read error storage\n");
    return &error_storage;
}

void my_nvs_save_error(my_err_t err, uint16_t arg)
{
    static_assert(MY_ERR_TOTAL < UINT16_MAX);
    
    uint32_t buffer[EEPROM_PAGE_WORDS] = { 0 };

    //Append new error to the list
    nvs_err_data_t* item = &(error_storage.error_data[error_storage.index++]);
    if (error_storage.index >= MY_NVS_ERROR_STORAGE_LEN) error_storage.index = 0;
    if (error_storage.count < UINT32_MAX) error_storage.count++;
    item->code = (uint16_t)err;
    item->arg = arg;
    error_storage.crc32 = GET_ERROR_STORAGE_CRC(&error_storage);
    xprintf("Saving err storage, CRC = 0x%08" PRIX32 "\n", error_storage.crc32);
    //Erase required EEPROM pages
    HAL_EEPROM_Erase(&heeprom, GET_PAGE_ADDR(EEPROM_ERROR_STORAGE_PAGE),
        EEPROM_PAGE_WORDS, HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT);
    //Copy struct into a page-wide buffer and write EEPROM
    for (size_t i = 0; i < error_storage_words; i++)
    {
        buffer[i] = ((uint32_t*)(&error_storage))[i];
    }
    if (HAL_EEPROM_Write(&heeprom, GET_PAGE_ADDR(EEPROM_ERROR_STORAGE_PAGE), buffer, EEPROM_PAGE_WORDS,
        HAL_EEPROM_WRITE_SINGLE, EEPROM_OP_TIMEOUT) != HAL_OK)
        xputs("Failed to save error storage\n");
}
void my_nvs_print_errors(void)
{
    size_t count = error_storage.count < MY_NVS_ERROR_STORAGE_LEN ? error_storage.count : MY_NVS_ERROR_STORAGE_LEN;
    for (size_t i = 0; i < count; i++)
    {
        const nvs_err_data_t* item = &(error_storage.error_data[i]);
        xprintf("%" PRIu32 ": 0x%04" PRIX32 " (0x%04" PRIX32 ")\n",
            i < error_storage.index ?
            (error_storage.count - error_storage.index + i) : 
            (error_storage.count - error_storage.index - MY_NVS_ERROR_STORAGE_LEN + i),
            (uint32_t)(item->code), (uint32_t)(item->arg));
    }
}
