#include "shared.h"

#define SEMAPHORE_WAIT_TIME_MS 10

esp_err_t data_service_init(car_state_t* state, uint32_t* LoRa_array, uint32_t array_length);

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len);

car_state_t * data_service_get_car_state();

esp_err_t data_service_write(uint32_t index, uint32_t data);

esp_err_t data_service_write_global_time(uint8_t* frame_data);

esp_err_t data_service_handle_semaphor(SemaphoreHandle_t checkSemaphore, SemaphoreHandle_t takeSemaphore);
