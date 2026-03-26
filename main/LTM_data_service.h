#ifndef LTM_DATA_SERVICE_H
#define LTM_DATA_SERVICE_H
#include "shared.h"

#define SEMAPHORE_WAIT_TIME_MS 100
#define PADDOCK_SERIAL_BUFFER_SIZE 2048

esp_err_t data_service_init(car_state_t* state, uint32_t* LoRa_array, uint32_t array_length);

esp_err_t data_service_init_paddock(paddock_array_t* array);

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len, int car_num);

car_state_t * data_service_get_car_state();

esp_err_t data_service_write(uint32_t index, uint32_t data, uint8_t len);

esp_err_t data_service_write_global_time(uint8_t* frame_data);

esp_err_t data_service_handle_semaphor(SemaphoreHandle_t checkSemaphore, SemaphoreHandle_t takeSemaphore);

// Returns a counter that increments on every data_service_write() call.
// Used by the enqueue task to detect whether fresh CAN data has arrived since the last push.
uint32_t data_service_get_write_seq(void);

// Reads/increments the persistent marker counter from SD, sets car_state->marker,
// and arms a 100ms one-shot timer to clear it. Call from espnow_enqueue_task only.
void data_service_trigger_marker(void);

// Deletes /sd_card/.marker.txt to reset the marker counter on a fresh session.
void data_service_reset_marker_file(void);

#endif //LTM_DATA_SERVICE_H