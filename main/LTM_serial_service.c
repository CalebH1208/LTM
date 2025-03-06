#include "LTM_serial_service.h"

static const char *TAG = "Serial_Service";

paddock_array_t* paddock_array;

static bool clear_on_next_printf = false;

esp_err_t serial_service_init(paddock_array_t* array){
    paddock_array = array;
    
    // other serial initialization stuff

    ESP_LOGI(TAG, "Init Success");
    return ESP_OK;
}

void serial_clear_on_next_printf(bool state){
    clear_on_next_printf = state;
}

void compile_buffer(char* serial_buffer, paddock_element_t* elements, uint32_t* data_array, uint16_t length, uint16_t car_num);

esp_err_t serial_send(uint8_t* data, uint16_t length){
    uint32_t* data_array = (uint32_t*)data;
    //printf("length: %d\n",length);
    length /= 4;

    if(clear_on_next_printf){
        printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        serial_clear_on_next_printf(false);
    }

    char serial_buffer[SERIAL_BUFFER_LENGTH];
    for(int i = 0; i < paddock_array->num_cars; i++){
        if(data_array[0] == paddock_array->cars[i].car_number){
            compile_buffer(serial_buffer, paddock_array->cars[i].elements, data_array, paddock_array->cars[i].array_length, paddock_array->cars[i].car_number);
            // ESP_LOGI(TAG,"Started print");
            printf("%s", serial_buffer);
            // ESP_LOGI(TAG,"stopped print");
            break;
        }
    }

    return ESP_OK;
}

void compile_buffer(char* serial_buffer, paddock_element_t* elements, uint32_t* data_array, uint16_t length, uint16_t car_num){
    printf("length: %d\n",length);
    value_union_t data;
    uint32_t buffer_index = 0;
    buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "Car Number: %d\n", car_num);
    //printf("car number buffer index: %ld\n",buffer_index);
    for(int i = 0; i < length; i++){
        data.u = data_array[i+1];
        float data_float;
        if(elements[i].type == 'f'){
            data_float = data.f;
        }else if(elements[i].type == 'u'){
            data_float = (float) data.u;
        }else if(elements[i].type == 'i'){
            data_float = (float) data.i;
        }else{
            data_float = 0;
        }

        data_float *= elements[i].conversion;

        buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "%s, %s, %.7f\n", elements[i].name, elements[i].unit, data_float);
        //printf("buffer_index: %ld\n",buffer_index);
    }
    //printf("placing buffer end at: %ld\n",buffer_index);
    serial_buffer[buffer_index] = '\0';
}