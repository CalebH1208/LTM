#ifndef INIT_H
#define INIT_H

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "cJSON.h"

#include "shared.h"

#include "LTM_data_logging.h"
#include "LTM_LoRa.h"
#include "LTM_CAN.h"
#include "LTM_data_service.h"

#define NAME_MAX_BUFFER_LEN 32
#define UNIT_MAX_BUFFER_LEN 8

#define SD_SLOT 1
#define SD_BUS_WIDTH 4
#define SD_BUS_FREQ 40000

typedef struct {
    valid_CAN_speeds_t* bus_speed;
    car_state_t* car_state;
    int* global_time_id;
    CAN_metadata_t* can_data;
    uint32_t** LoRa_array;
    uint32_t* LoRa_array_length;
    data_value_t*** CAN_ID_array;
}init_parameters;

typedef struct init_structure{
    char name[NAME_MAX_BUFFER_LEN];
    float conversion;
    char unit[UNIT_MAX_BUFFER_LEN];
    uint32_t precision;
}init_struct_t;

esp_err_t SD_init(sdmmc_slot_config_t* config);

esp_err_t parse_JSON_globals(valid_CAN_speeds_t* bus_speed, data_value_t*** CAN_ID_array, car_state_t* car_state);

esp_err_t parse_JSON_CAN(CAN_metadata_t* can_data, car_state_t* car_state, uint32_t** LoRa_array, uint32_t* LoRa_array_length);

esp_err_t write_header();

#endif //INIT_H