#ifndef LTM_DATA_LOGGING_H
#define LTM_DATA_LOGGING_H

#include "shared.h"
#include "LTM_data_service.h"
#include "esp_mac.h"

#define BUFFER_LENGTH 1024

#define PATH_100HZ MOUNT_POINT"/100HzLog.csv"
#define PATH_10HZ MOUNT_POINT"/10HzLog.csv"
#define PATH_1HZ MOUNT_POINT"/1HzLog.csv"

void data_logging_init(uint32_t * indices_100Hz_p, uint32_t length_100Hz_p, uint32_t * indices_10Hz_p, uint32_t length_10Hz_p, uint32_t * indices_1Hz_p, uint32_t length_1Hz_p);

void data_logging_ritual();

char * parse_data_string(uint32_t * indices, uint32_t num_indices, car_state_t * state, size_t * str_len, int64_t time);

void write_data(char * data, size_t data_length, FILE * fptr);

#endif //LTM_DATA_LOGGING_H