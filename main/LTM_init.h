#ifndef INIT_H
#define INIT_H

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "shared.h"

#include "LTM_data_logging.h"
#include "LTM_LoRa.h"
#include "LTM_CAN.h"
#include "LTM_data_service.h"

#define SD_SLOT 0
#define SD_BUS_WIDTH 1

#define SD_BUS_FREQ 20000

#define SPI_SPEED 10000000
#define LORA_DEFAULT_FREQUENCY 915000000

typedef struct {
    LTM_type_t* LTM_type;
    valid_CAN_speeds_t* bus_speed;
    car_state_t* car_state;
    paddock_array_t* paddock_array;
    int* global_time_id;
    CAN_metadata_t* can_data;
    uint32_t** LoRa_array;
    uint32_t* LoRa_array_length;
    data_value_t*** CAN_ID_array;
    uint64_t* LoRa_freq;
    uint64_t* LoRa_channels;
}init_parameters;

typedef struct init_structure{
    char name[NAME_MAX_BUFFER_LEN];
    float conversion;
    char unit[UNIT_MAX_BUFFER_LEN];
    uint32_t precision;
}init_struct_t;

esp_err_t SD_init(sdmmc_slot_config_t* config);

esp_err_t parse_JSON_globals(init_parameters params);

esp_err_t finish_initialization();

esp_err_t write_header();

esp_err_t write_header_helper(FILE* file,uint32_t * indices, uint32_t num_indices);

#endif //INIT_H