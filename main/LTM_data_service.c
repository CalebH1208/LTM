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

    ESP_LOGI(TAG, "Data Service Init Success");
    return ESP_OK;
}

esp_err_t data_service_get_LoRa_data(uint8_t * data, uint16_t * len){
    car_element_t * temp_elements_copy = malloc(car_state->data_length * sizeof(car_element_t));

    if(temp_elements_copy == NULL){
        return ESP_ERR_NO_MEM;
    }

    if(data_service_handle_semaphor(write_lock, read_lock) != ESP_OK){
        free(temp_elements_copy);
        return ESP_ERR_INVALID_RESPONSE;
    }
    reader_count++;

    memcpy(temp_elements_copy, car_state->elements, car_state->data_length * sizeof(car_element_t));

    reader_count--;
    if(0 == reader_count){
        xSemaphoreGive(read_lock);
    }

    uint32_t * full_val = (uint32_t *)data;

    for(uint32_t i = 0; i < LoRa_adrr_array_length; i++){
        full_val[i] = temp_elements_copy[LoRa_adrr_array[i]].data.u;
    }

    *len = sizeof(uint32_t) * LoRa_adrr_array_length;

    return ESP_OK;
}

car_state_t * data_service_get_car_state(){
    car_state_t * temp_state_copy = malloc(sizeof(car_state_t));
    if(temp_state_copy == NULL){
        return NULL;
    }

    temp_state_copy->elements = malloc(car_state->data_length * sizeof(car_element_t));

    if(temp_state_copy->elements == NULL){
        free(temp_state_copy);
        return NULL;
    }

    if(data_service_handle_semaphor(write_lock, read_lock) != ESP_OK){
        return NULL;
    }
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
    if(data_service_handle_semaphor(read_lock, write_lock) != ESP_OK){
        return ESP_ERR_INVALID_RESPONSE;
    }
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

esp_err_t data_service_handle_semaphor(SemaphoreHandle_t checkSemaphore, SemaphoreHandle_t takeSemaphore){
    if(xSemaphoreTake(checkSemaphore, pdMS_TO_TICKS(SEMAPHORE_WAIT_TIME_MS)) != pdTRUE){
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(takeSemaphore, 0);
    xSemaphoreGive(checkSemaphore);
    return ESP_OK;
}
