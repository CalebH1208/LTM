#include "LTM_data_service.h"

static const char *TAG = "Data_Service";

static int reader_count;
static SemaphoreHandle_t read_lock;
StaticSemaphore_t read_lock_buffer;
static int writer_count;
static SemaphoreHandle_t write_lock;
StaticSemaphore_t write_lock_buffer;

static car_state_t* car_state;

static uint32_t* LoRa_adrr_array;
static uint32_t LoRa_adrr_array_length;

esp_err_t data_service_init(car_state_t* state, uint32_t* LoRa_array, uint32_t array_length){
    car_state = state;
    LoRa_adrr_array = LoRa_array;
    LoRa_adrr_array_length = array_length;

    read_lock = xSemaphoreCreateBinaryStatic(&read_lock_buffer);
    write_lock = xSemaphoreCreateBinaryStatic(&write_lock_buffer);

    xSemaphoreGive(read_lock);
    xSemaphoreGive(write_lock);

    ESP_LOGI(TAG, "Data Service Init Success");
    return ESP_OK;
}

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len, int car_num){
    car_element_t * temp_elements_copy = malloc(car_state->data_length * sizeof(car_element_t));

    if(temp_elements_copy == NULL){
        return ESP_ERR_NO_MEM;
    }

    //ESP_LOGW(TAG, "attempting to take semphore lora data, r: %d  |  w:%d",reader_count, writer_count);
    if(data_service_handle_semaphor(write_lock, read_lock) != ESP_OK){
        free(temp_elements_copy);
        return ESP_ERR_INVALID_RESPONSE;
    }
    //ESP_LOGW(TAG, "success lora state, r: %d  |  w:%d",reader_count, writer_count);
    reader_count++;

    memcpy(temp_elements_copy, car_state->elements, car_state->data_length * sizeof(car_element_t));

    reader_count--;
    if(0 == reader_count){
        xSemaphoreGive(read_lock);
    }

    uint32_t * full_val = (uint32_t *)data;
    full_val[0] = car_num;

    for(uint32_t i = 0; i < LoRa_adrr_array_length; i++){
        full_val[i+1] = temp_elements_copy[LoRa_adrr_array[i]].data.u;
    }

    *len = sizeof(uint32_t) * (LoRa_adrr_array_length+1);

    //ESP_LOGW(TAG,"length of data: %d, %ld, %ld, %ld, %ld",*len,full_val[0],full_val[1],full_val[2],full_val[3]);
    free(temp_elements_copy);
    return ESP_OK;
}

car_state_t * data_service_get_car_state(){
    car_state_t * temp_state_copy = malloc(sizeof(car_state_t));
    if(temp_state_copy == NULL){
        ESP_LOGE(TAG, "malloc failed on temp state cpy");
        return NULL;
    }

    temp_state_copy->elements = malloc(car_state->data_length * sizeof(car_element_t));

    if(temp_state_copy->elements == NULL){
        free(temp_state_copy);
        ESP_LOGE(TAG, "malloc failed on elements shit");
        return NULL;
    }
    // ESP_LOGW(TAG, "attempting to take semphore car state, r: %d  |  w:%d",reader_count, writer_count);
    if(data_service_handle_semaphor(write_lock, read_lock) != ESP_OK){
        free(temp_state_copy);
        ESP_LOGE(TAG, "semaphor timed out");
        return NULL;
    }
    // ESP_LOGW(TAG, "success car state, r: %d  |  w:%d",reader_count, writer_count);
    reader_count++;

    temp_state_copy->car_number = car_state->car_number;
    temp_state_copy->data_length = car_state->data_length;
    temp_state_copy->time = car_state->time;
    memcpy(temp_state_copy->elements, car_state->elements, car_state->data_length * sizeof(car_element_t));

    reader_count--;
    if(0 == reader_count){
        xSemaphoreGive(read_lock);
    }

    return temp_state_copy;
}

esp_err_t data_service_write(uint32_t index, uint32_t data){
    // ESP_LOGW(TAG, "attempting to take semphore write, r: %d  |  w:%d",reader_count, writer_count);
    if(data_service_handle_semaphor(read_lock, write_lock) != ESP_OK){
        return ESP_ERR_INVALID_RESPONSE;
    }
    //ESP_LOGW(TAG, "success write, r: %d  |  w:%d",reader_count, writer_count);
    writer_count++;

    car_state->elements[index].data.u = data;

    writer_count--;
    if(0 == writer_count){
        xSemaphoreGive(write_lock);
    }

    return ESP_OK;
}

esp_err_t data_service_write_global_time(uint8_t* frame_data){
    if(data_service_handle_semaphor(read_lock, write_lock) != ESP_OK){
        return ESP_ERR_INVALID_RESPONSE;
    }
    writer_count++;

    car_state->time.year = frame_data[0];
    car_state->time.month = frame_data[1];
    car_state->time.day = frame_data[2];
    car_state->time.hour = frame_data[3];
    car_state->time.min = frame_data[4];
    car_state->time.sec = frame_data[5];
    car_state->time.msec = (frame_data[6] << 8) | frame_data[7];

    writer_count--;
    if(0 == writer_count){
        xSemaphoreGive(write_lock);
    }
    
    return ESP_OK;
}

esp_err_t data_service_write_paddock(uint32_t index, uint32_t data){
    // ESP_LOGW(TAG, "attempting to take semphore write, r: %d  |  w:%d",reader_count, writer_count);
    if(data_service_handle_semaphor(read_lock, write_lock) != ESP_OK){
        return ESP_ERR_INVALID_RESPONSE;
    }
    //ESP_LOGW(TAG, "success write, r: %d  |  w:%d",reader_count, writer_count);
    writer_count++;

    car_state->elements[index].data.u = data;

    writer_count--;
    if(0 == writer_count){
        xSemaphoreGive(write_lock);
    }

    return ESP_OK;
}

// if(data_service_handle_semaphor(write_lock, read_lock) != ESP_OK){
esp_err_t data_service_handle_semaphor(SemaphoreHandle_t checkSemaphore, SemaphoreHandle_t takeSemaphore){
    if(xSemaphoreTake(checkSemaphore, pdMS_TO_TICKS(SEMAPHORE_WAIT_TIME_MS)) != pdTRUE){
        ESP_LOGE(TAG,"semaphore error check: %d  | take: %d",uxSemaphoreGetCount(checkSemaphore), uxSemaphoreGetCount(takeSemaphore));
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(takeSemaphore, 0);
    xSemaphoreGive(checkSemaphore);
    return ESP_OK;
}
