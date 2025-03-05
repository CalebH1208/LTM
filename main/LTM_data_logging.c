#include "LTM_data_logging.h"

static const char *TAG = "Data Logging";

static uint32_t * indices_100Hz;
static uint32_t length_100Hz;

static uint32_t * indices_10Hz;
static uint32_t length_10Hz;

static uint32_t * indices_1Hz;
static uint32_t length_1Hz;

esp_err_t data_logging_init(CAN_metadata_t CAN_data){
    indices_100Hz = CAN_data.indices_100Hz_p;
    length_100Hz = CAN_data.length_100Hz_p;
    indices_10Hz = CAN_data.indices_10Hz_p;
    length_10Hz = CAN_data.length_10Hz_p;
    indices_1Hz = CAN_data.indices_1Hz_p;
    length_1Hz = CAN_data.length_1Hz_p;
    ESP_LOGI(TAG,"Data logging init successful");
    return ESP_OK;
}

void data_logging_ritual(){
    FILE * fptr_100HZ = fopen(PATH_100HZ, "a");
    FILE * fptr_10HZ = fopen(PATH_10HZ, "a");
    FILE * fptr_1HZ = fopen(PATH_1HZ, "a");

    if(NULL == fptr_100HZ || NULL == fptr_10HZ || NULL == fptr_1HZ){
        ESP_LOGE(TAG, "The data logging files failed to open");
    }

    TickType_t curr_ticks;

    uint32_t count = 0;
    car_state_t * state;

    size_t str_len;
    struct timeval up_time;
    int64_t up_time_ms;

    char * csv_string;

    while(1){
        curr_ticks = xTaskGetTickCount();

        state = data_service_get_car_state();

        if(state == NULL){
            ESP_LOGE(TAG, "data service fail");
            count++;
            //ESP_LOGI(TAG,"this is the count: %ld",count);
            xTaskDelayUntil(&curr_ticks, pdMS_TO_TICKS(10));
            RUN_THAT_SHIT_BACK;
        }

        // ESP_LOGI(TAG,"State Info: %d %ld",state->time.day, state->car_number);

        str_len = 0;
        
        gettimeofday(&up_time,NULL);
        up_time_ms = (int64_t)up_time.tv_sec * 1000000L + (int64_t)up_time.tv_usec;

        csv_string = parse_data_string(indices_100Hz, length_100Hz, state, &str_len, up_time_ms);
        //ESP_LOGI(TAG,"csv string: %s | strlen: %d",csv_string,str_len);
        write_data(csv_string, str_len, fptr_100HZ);

        if(count % 10 == 0){
            csv_string = parse_data_string(indices_10Hz, length_10Hz, state, &str_len, up_time_ms);
            write_data(csv_string, str_len, fptr_10HZ);
        }

        if(count % 100 == 0){
            csv_string = parse_data_string(indices_1Hz, length_1Hz, state, &str_len, up_time_ms);
            write_data(csv_string, str_len, fptr_1HZ);

            //fflush(fptr_100HZ);
            // fflush(fptr_10HZ);
            // fflush(fptr_1HZ);
            // fsync();
            
            fclose(fptr_100HZ);
            fclose(fptr_10HZ);
            fclose(fptr_1HZ);

            fptr_100HZ = fopen(PATH_100HZ, "a");
            fptr_10HZ = fopen(PATH_10HZ, "a");
            fptr_1HZ = fopen(PATH_1HZ, "a");

        }

        count++;
        
        free(state->elements);
        free(state);
        // ESP_LOGI(TAG,"this is the count: %ld",count);
        xTaskDelayUntil(&curr_ticks, pdMS_TO_TICKS(10));
    }
}

char * parse_data_string(uint32_t * indices, uint32_t num_indices, car_state_t * state, size_t * str_len, int64_t up_time){
    char * buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(buffer == NULL){
        return buffer;
    }

    size_t buffer_index = 0;
    size_t bytes_written = 0;

    global_time_t time = state->time;

    bytes_written = snprintf(buffer, BUFFER_LENGTH, "%lld,%u/%u/%u_%u:%u:%u.%u", up_time, time.month, time.day, time.year, time.hour, time.min, time.sec, time.msec);
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        free(buffer);
        return NULL;
    }

    //if(num_indices >0)ESP_LOGI(TAG,"%c\n",state->elements[0].type);
    for(int i = 0; i < num_indices; i++){
        if(state->elements[i].type == 'f'){
            bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%.7f", state->elements[indices[i]].data.f);
            if(bytes_written > 0){
                buffer_index += bytes_written;
            }else{
                free(buffer);
                return NULL;
            }
        }else if(state->elements[i].type == 'u'){
            bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%lu", state->elements[indices[i]].data.u);
            if(bytes_written > 0){
                buffer_index += bytes_written;
            }else{
                free(buffer);
                return NULL;
            }
        }else if(state->elements[i].type == 'i'){
            bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%ld", state->elements[indices[i]].data.i);
            if(bytes_written > 0){
                buffer_index += bytes_written;
            }else{
                free(buffer);
                return NULL;
            }
        }
        
    }
    
    buffer[buffer_index] = '\n';
    buffer[++buffer_index] = '\0';
    *str_len = buffer_index+1;
    return buffer;
}

void write_data(char * data, size_t data_length, FILE * fptr){
    if(data == NULL)return;
    if(data_length == 0 || fptr == NULL){
        free(data);
        return;
    }
    fwrite(data, sizeof(uint8_t), data_length, fptr);
    free(data);
}

void write_data_no_free(char * data, size_t data_length, FILE * fptr){
    if(data == NULL)return;
    if(data_length == 0 || fptr == NULL){
        free(data);
        return;
    }
    fwrite(data, sizeof(uint8_t), data_length, fptr);
}
