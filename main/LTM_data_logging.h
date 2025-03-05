#ifndef LTM_DATA_LOGGING_H
#define LTM_DATA_LOGGING_H

#include "shared.h"
#include "LTM_data_service.h"
#include "esp_mac.h"
#include <sys/time.h>

#define BUFFER_LENGTH 1024

esp_err_t data_logging_init(CAN_metadata_t CAN_data);

void data_logging_ritual();

char * parse_data_string(uint32_t * indices, uint32_t num_indices, car_state_t * state, size_t * str_len, int64_t time);

void write_data(char * data, size_t data_length, FILE * fptr);

void write_data_no_free(char * data, size_t data_length, FILE * fptr);

#endif //LTM_DATA_LOGGING_H