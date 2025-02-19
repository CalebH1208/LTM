#ifndef LTM_CAN_H
#define LTM_CAN_H

#include "shared.h"
#include "LTM_data_service.h"
#include "driver/twai.h"

// allowable messages are between 700 - 7FF
// look at esp docs before changing this twai_filter_config_t (or just don't use these your probably fine)
#define CAN_ACCEPTANCE_CODE 0x700
#define CAN_ACCEPTANCE_MASK 0x7FF

esp_err_t CAN_init(gpio_num_t CAN_tx, gpio_num_t CAN_rx, valid_CAN_speeds_t bus_speed, data_value_t* CAN_ID_array);

void CAN_ritual();

void parse_and_store_frame(data_value_t* head, uint8_t* frame);

#endif //LTM_CAN_H