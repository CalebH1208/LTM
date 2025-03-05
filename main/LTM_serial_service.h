#ifndef LTM_SERIAL_H
#define LTM_SERIAL_H
#include "shared.h"

#define SERIAL_BUFFER_LENGTH 1024

esp_err_t serial_service_init(paddock_array_t* array);

esp_err_t serial_send(uint8_t* data, uint16_t length);

#endif //LTM_SERIAL_H