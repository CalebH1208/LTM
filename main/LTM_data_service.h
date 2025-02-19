#include "shared.h"

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len);

car_state_t * data_service_get_car_state();

void data_service_write(int index, uint32_t data);

data_service_write_global_time(uint8_t* frame_data);