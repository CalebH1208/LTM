#ifndef LTM_DATA_SERVICE_H
#define LTM_DATA_SERVICE_H
#include "shared.h"

#define SEMAPHORE_WAIT_TIME_MS 100
#define PADDOCK_SERIAL_BUFFER_SIZE 2048

esp_err_t data_service_init(car_state_t* state, uint32_t* LoRa_array, uint32_t array_length);

esp_err_t data_service_init_paddock(paddock_array_t* array);

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len, int car_num);

car_state_t * data_service_get_car_state();

esp_err_t data_service_write(uint32_t index, uint32_t data);

esp_err_t data_service_write_global_time(uint8_t* frame_data);

esp_err_t data_service_handle_semaphor(SemaphoreHandle_t checkSemaphore, SemaphoreHandle_t takeSemaphore);

#endif //LTM_DATA_SERVICE_H