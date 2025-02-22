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

#define MOUNT_POINT "/sd_card"

#define PATH_100HZ MOUNT_POINT"/100HzLog.csv"
#define PATH_10HZ MOUNT_POINT"/10HzLog.csv"
#define PATH_1HZ MOUNT_POINT"/1HzLog.csv"

#define MAX_CAN_ID_COUNT 256

#define RUN_THAT_SHIT_BACK continue // are you having fun yet?
#define LOOP_AGAIN_MOTHERFUCKER continue // calebs version of fun

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

typedef struct data_value{
    uint32_t index;
    uint8_t offset;
    uint8_t length;
    char type;
    struct data_value* next;
}data_value_t;

#endif //SHARED_H