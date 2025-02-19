#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_task_wdt.h"

#include "LEDs.h"
#include "LoRa.h"

#include "shared.h"

#include "LTM_data_logging.h"
#include "LTM_LoRa.h"

#include "LTM_data_service.h"

bool is_sender = false;

#define NSS 10
#define MOSI 11
#define CLK 12
#define MISO 13
#define RST 48
#define DIO0 21
#define DIO1 47


static const char *TAG = "LTM";

char * parse_header_string(uint32_t * indices, uint32_t num_indices, car_state_t * state, size_t * str_len){
    char * names_buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(NULL == names_buffer){
        return NULL;
    }
    
    char * units_buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(NULL == units_buffer){
        free(names_buffer);
        return NULL;
    }

    char * conversion_buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(NULL == conversion_buffer ){
        free(names_buffer);
        free(units_buffer);
        return NULL;
    }

    char* precision_buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(NULL == precision_buffer){
        free(names_buffer);
        free(units_buffer);
        free(conversion_buffer);
        return NULL;
    }
    
    size_t buffer_index = 0;
    size_t bytes_written = 0;


    bytes_written = snprintf(buffer, BUFFER_LENGTH, "Time, Global Time");
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        return NULL;
    }

    for(int i = 0; i < num_indices; i++){
        
            bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%s", state->elements[i]);
            if(bytes_written > 0){
                buffer_index += bytes_written;
            }else{
                return NULL;
            }
        
    }

    buffer[buffer_index] = '\n';
    buffer[buffer_index + 1] = '\0';

    return buffer;
}

void app_main(void){


    
}
