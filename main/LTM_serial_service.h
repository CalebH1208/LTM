#ifndef LTM_SERIAL_H
#define LTM_SERIAL_H
#include "shared.h"
#include "stdio_ext.h"

#define SERIAL_BUFFER_LENGTH 1024

esp_err_t serial_service_init(paddock_array_t* array);

esp_err_t serial_send(uint8_t* data, uint16_t length);

void serial_clear_on_next_printf(bool state);

#endif //LTM_SERIAL_H