#ifndef LTM_SERIAL_H
#define LTM_SERIAL_H
#include "shared.h"
#include "stdio_ext.h"

#define SERIAL_BUFFER_LENGTH 1024

esp_err_t serial_service_init(paddock_array_t* array);

esp_err_t serial_send(uint8_t* data, uint16_t length, uint32_t timestamp_ms);

void serial_clear_on_next_printf(bool state);

// PADDOCK-side task: reads UART0 for inbound commands (e.g. "MARK:<car_num>\n")
// and forwards them to the appropriate car via ESP-NOW.
// Must be created after serial_service_init() and espnow_init().
void serial_rx_task(void* params);

#endif //LTM_SERIAL_H