#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "sdkconfig.h"
#include "LEDs.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"

#define MOUNT_POINT "/sd_card"

#define PATH_100HZ MOUNT_POINT"/100HzLog.csv"
#define PATH_10HZ MOUNT_POINT"/10HzLog.csv"
#define PATH_1HZ MOUNT_POINT"/1HzLog.csv"

#define MAX_CAN_ID_COUNT 256

#define NAME_MAX_BUFFER_LEN 32
#define UNIT_MAX_BUFFER_LEN 8

#define RUN_THAT_SHIT_BACK continue // are you having fun yet?
#define LOOP_AGAIN_MOTHERFUCKER continue // calebs version of fun

typedef enum{
    PADDOCK,
    CAR
}LTM_type_t;

typedef enum{
    CAN_1MBITS,
    CAN_500KBITS,
    CAN_250KBITS,
    CAN_100KBITS,
    CAN_50KBITS
}valid_CAN_speeds_t;

typedef union{
    float f;
    uint32_t u;
    int32_t i;
}value_union_t;

typedef struct{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint16_t msec;
}global_time_t;

typedef struct{
    value_union_t data;
    char type;
}car_element_t;

typedef struct{
    int32_t car_number;
    uint32_t data_length;
    global_time_t time;
    car_element_t * elements;
}car_state_t;

typedef struct{
    char name[NAME_MAX_BUFFER_LEN];
    float conversion;
    char unit[UNIT_MAX_BUFFER_LEN];
    char type;
    value_union_t data;
}paddock_element_t;

typedef struct{
    int32_t car_number;
    int32_t array_length;
    paddock_element_t* elements;
}paddock_state_t;

typedef struct{
    uint8_t num_cars;
    paddock_state_t * cars;
}paddock_array_t;

typedef struct data_value{
    uint32_t index;
    uint8_t offset;
    uint8_t length;
    struct data_value* next;
}data_value_t;

typedef struct{
    uint32_t * indices_100Hz_p;
    uint32_t length_100Hz_p;
    uint32_t * indices_10Hz_p;
    uint32_t length_10Hz_p;
    uint32_t * indices_1Hz_p;
    uint32_t length_1Hz_p;
}CAN_metadata_t;

#endif //SHARED_H