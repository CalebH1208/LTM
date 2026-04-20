#include "LTM_LapTimer.h"

#define TAG ((const char *const) "Lap_Time")

static lt_timeval previous_time;

esp_err_t lap_timer_init() {
    gettimeofday(&previous_time, NULL);
    ESP_LOGI(TAG, "Lap Timer initialized");
    return ESP_OK;
}

void seg_triggered(laptimer_t* laptimer) { 
    lt_timeval current_time;
    gettimeofday(&current_time, NULL);
    
    //Convert current and last times to uint64_t
    uint64_t currentTimeUs = (uint64_t) current_time.tv_sec * 1000000 + current_time.tv_usec;
    uint64_t previousTimeUs = (uint64_t) previous_time.tv_sec * 1000000 + previous_time.tv_usec;

    //Total time elapsed in microseconds (Us)
    uint64_t totalMicroseconds = currentTimeUs - previousTimeUs;

    // DEBUG: printf("%lld\n", (uint64_t) totalMicroseconds);
    if (totalMicroseconds < VALID_SEGMENT_TRIGGER_US) { //Check if trigger is valid
        ESP_LOGW ("Lap too short, ignoring", "Time: %d:%02d.%06llu",
            (totalMicroseconds / 60000000), ((totalMicroseconds % 60000000) / 1000000), (totalMicroseconds % 1000000));
        return;
    }

    //Get minutes and seconds from totalMicroseconds
    uint16_t minutes = totalMicroseconds / 60000000; 
    uint8_t seconds = (totalMicroseconds % 60000000) / 1000000;

    //Update the timer
    laptimer->microSeconds = totalMicroseconds % 1000000; //Remainder as microseconds
    laptimer->minutes = minutes;
    laptimer->seconds = seconds;

    ESP_LOGW ("Updated lap time", "Time: %d:%02d.%06llu",
        (minutes), (seconds), (uint64_t) laptimer->microSeconds);
}

esp_err_t compose_LT_data(uint8_t* data) {
    if(data == NULL) return ESP_ERR_INVALID_ARG;

    laptimer_t laptimer;
    seg_triggered(&laptimer);

    // "LT": <index>, <min>:<sec>::<ms>

    //Lap Time Data
    data[0] = laptimer.minutes % 256;
    data[1] = laptimer.seconds;

    int milliSec = laptimer.microSeconds/1000;
    data[2] = milliSec/256;
    data[3] = milliSec % 256;
    
    return ESP_OK;
}
#undef TAG