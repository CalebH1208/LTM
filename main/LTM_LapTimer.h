#ifndef LTM_LAPTIMER_H
#define LTM_LAPTIMER_H

#include "shared.h"

// #include LT stuff
#include <sys/time.h>
#include "esp_timer.h"


#define VALID_SEGMENT_TRIGGER_US 1000000 // Minimum valid segment trigger time in microseconds (1 second)

typedef struct {
    uint8_t minutes;
    uint8_t seconds;
    uint16_t microSeconds;
} laptimer_t;

typedef struct timeval lt_timeval;

esp_err_t lap_timer_init();

int seg_triggered(laptimer_t* laptimer);

esp_err_t compose_LT_data(uint8_t* data);

#endif // LTM_LAPTIMER_H