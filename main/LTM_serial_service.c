#include "LTM_serial_service.h"
#include "LTM_espnow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "Serial_Service";

paddock_array_t* paddock_array;

static bool clear_on_next_printf = false;

esp_err_t serial_service_init(paddock_array_t* array){
    paddock_array = array;
    // No driver install needed — USB Serial/JTAG console owns the peripheral.
    // stdin RX is available via fgetc() after menuconfig sets console to USB Serial/JTAG.
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_LOGI(TAG, "Init Success");
    return ESP_OK;
}

void serial_clear_on_next_printf(bool state){
    clear_on_next_printf = state;
}

void compile_buffer(char* serial_buffer, paddock_element_t* elements, uint32_t* data_array, uint16_t length, int16_t car_num, uint32_t timestamp_ms, uint16_t rx_word_count);

esp_err_t serial_send(uint8_t* data, uint16_t length, uint32_t timestamp_ms){
    uint32_t* data_array = (uint32_t*)data;
    //printf("length: %d\n",length);
    uint16_t rx_word_count = length / 4;
    length /= 4;
    clear_on_next_printf = true;
    if(clear_on_next_printf){
        printf("\n\n");
        serial_clear_on_next_printf(false);


        char serial_buffer[SERIAL_BUFFER_LENGTH];
        for(int i = 0; i < paddock_array->num_cars; i++){
            if(data_array[0] == paddock_array->cars[i].car_number){
                compile_buffer(serial_buffer, paddock_array->cars[i].elements, data_array, paddock_array->cars[i].array_length, paddock_array->cars[i].car_number, timestamp_ms, rx_word_count);
                // ESP_LOGI(TAG,"Started print");
                printf("%s", serial_buffer);
                // ESP_LOGI(TAG,"stopped print");
                break;
            }
        }
        if(data_array[0] == -1){
            uint8_t* data = (uint8_t*) data_array;
                                                                            //LT:=[segment number],[lap number],[minutes]:[seconds].[milliseconds]
            snprintf(serial_buffer , SERIAL_BUFFER_LENGTH , "LT:=%d,%d,%d:%d.%d\n|",data[12], data[13], data[4],data[5], (data[6] << 8) | data[7]);
            printf("%s",serial_buffer);
        }

        // Yield after printf to allow IDLE task to run and reset watchdog
        // This prevents watchdog timeouts when printf blocks for extended periods
        taskYIELD();
    }

    return ESP_OK;
}

void compile_buffer(char* serial_buffer, paddock_element_t* elements, uint32_t* data_array, uint16_t length, int16_t car_num, uint32_t timestamp_ms, uint16_t rx_word_count){
    //printf("length: %d\n",length);
    value_union_t data;
    uint32_t buffer_index = 0;
    buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "CN:%d\n", car_num);
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

        buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "%s,%s,%.7f\n", elements[i].name, elements[i].unit, data_float);
        //printf("buffer_index: %ld\n",buffer_index);
    }
    // Append MARKER line if packet contains the extra marker word and value is non-zero
    // Packet layout: [car_num(1)][data(length)][marker(1)] = length+2 words total
    if(rx_word_count >= (uint16_t)(length + 2)){
        uint32_t marker_val = data_array[length + 1];
        if(marker_val > 0){
            buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "MARKER,%lu\n", (unsigned long)marker_val);
        }
    }
    if(timestamp_ms > 0){
        buffer_index += snprintf(serial_buffer + buffer_index, SERIAL_BUFFER_LENGTH - buffer_index, "TS:%lums\n", (unsigned long)timestamp_ms);
    }
    //printf("placing buffer end at: %ld\n",buffer_index);
    serial_buffer[buffer_index++] = '|';
    serial_buffer[buffer_index] = '\0';
}

#define SERIAL_LINE_MAX     64

void serial_rx_task(void* params) {

    char line[SERIAL_LINE_MAX];
    int line_len = 0;

    ESP_LOGI(TAG, "Serial RX task started");

    while (1) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char c = (char)ch;
        if (c == '\n' || c == '\r') {
            if (line_len > 0) {
                line[line_len] = '\0';
                if (strncmp(line, "MARK:", 5) == 0) {
                    int32_t car_num = (int32_t)atoi(line + 5);
                    ESP_LOGI(TAG, "MARK command received for car %ld", car_num);
                    for (int i = 0; i < paddock_array->num_cars; i++) {
                        if (paddock_array->cars[i].car_number == car_num) {
                            espnow_paddock_queue_marker(paddock_array->cars[i].mac);
                            break;
                        }
                    }
                }
                line_len = 0;
            }
        } else if (line_len < SERIAL_LINE_MAX - 1) {
            line[line_len++] = c;
        }
    }
}