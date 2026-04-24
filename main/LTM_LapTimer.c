#include "LTM_LapTimer.h"

#define TAG ((const char *const) "Lap_Time")

static lt_timeval previous_time;

esp_err_t lap_timer_init() {
    gettimeofday(&previous_time, NULL);
    ESP_LOGI(TAG, "Lap Timer initialized");
    return ESP_OK;
}

int seg_triggered(laptimer_t* laptimer) { 
    lt_timeval current_time;
    gettimeofday(&current_time, NULL);
    
    //Convert current and last times to uint64_t
    uint64_t currentTimeUs = (uint64_t) current_time.tv_sec * 1000000 + current_time.tv_usec;
    uint64_t previousTimeUs = (uint64_t) previous_time.tv_sec * 1000000 + previous_time.tv_usec;

    //Total time elapsed in microseconds (Us)
    uint64_t totalMicroseconds = currentTimeUs - previousTimeUs;

    if (totalMicroseconds < VALID_SEGMENT_TRIGGER_US) { //Check if trigger is valid
        // ESP_LOGW("Lap too short, ignoring", "Time: %d:%02d.%06" PRIu64,
        //     (int)(totalMicroseconds / 60000000),
        //     (int)((totalMicroseconds % 60000000) / 1000000),
        //     (uint64_t)(totalMicroseconds % 1000000));
        previous_time.tv_sec = current_time.tv_sec;
        previous_time.tv_usec = current_time.tv_usec;

        return 1;
    }

    //Get minutes and seconds from totalMicroseconds
    uint16_t minutes = totalMicroseconds / 60000000; 
    uint8_t seconds = (totalMicroseconds % 60000000) / 1000000;

    //Update the timer
    laptimer->millisec = (totalMicroseconds % 1000000) / 1000; //Remainder as microseconds
    laptimer->minutes = minutes;
    laptimer->seconds = seconds;

    previous_time.tv_sec = current_time.tv_sec;
    previous_time.tv_usec = current_time.tv_usec;

    // ESP_LOGW ("Updated lap time", "Time: %d:%02d.%03llu",
    //     (minutes), (seconds), (uint64_t) laptimer->millisec);

    return 0;
}

esp_err_t compose_LT_data(uint8_t* data) {
    if(data == NULL) return ESP_ERR_INVALID_ARG;

    laptimer_t laptimer = {0};
    if(seg_triggered(&laptimer)) {
        //Lap time was too short, do not update data
        return ESP_FAIL;
    }

    // "LT": <index>, <min>:<sec>::<ms>

    //Lap Time Data
    data[0] = laptimer.minutes % 256; //Store minutes in first byte (max 255 minutes) 
    data[1] = laptimer.seconds;

    data[2] = (laptimer.millisec >> 8) & 0xFF;  
    data[3] = laptimer.millisec & 0xFF;        
    
    return ESP_OK;
}
#undef TAG