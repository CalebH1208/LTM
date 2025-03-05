#include "LTM_CAN.h"

static const char *TAG = "LTM_CAN";

static data_value_t** CAN_IDs_list;

static int global_time_ID;

esp_err_t CAN_init(gpio_num_t CAN_tx, gpio_num_t CAN_rx, valid_CAN_speeds_t bus_speed, data_value_t** CAN_ID_array, int global_time_CAN_ID){
    CAN_IDs_list = CAN_ID_array;
    global_time_ID = global_time_CAN_ID;
    twai_general_config_t global_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_tx, CAN_rx, TWAI_MODE_NORMAL);
    twai_timing_config_t timing_config;
    switch(bus_speed){
        case(CAN_1MBITS):
            twai_timing_config_t timing_1000000 = TWAI_TIMING_CONFIG_1MBITS();
            timing_config = timing_1000000;
            break;
        case(CAN_500KBITS):
            twai_timing_config_t timing_500000 = TWAI_TIMING_CONFIG_500KBITS();
            timing_config = timing_500000;
            break;
        case(CAN_250KBITS):
            twai_timing_config_t timing_250000 = TWAI_TIMING_CONFIG_250KBITS();
            timing_config = timing_250000;
            break;
        case(CAN_100KBITS):
            twai_timing_config_t timing_100000 = TWAI_TIMING_CONFIG_100KBITS();
            timing_config = timing_100000;
            break;
        case(CAN_50KBITS):
            twai_timing_config_t timing_50000 = TWAI_TIMING_CONFIG_50KBITS();
            timing_config = timing_50000;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
   }
   twai_filter_config_t filter_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
//    filter_config.acceptance_code = CAN_ACCEPTANCE_CODE;
//    filter_config.acceptance_mask = CAN_ACCEPTANCE_MASK;
//    filter_config.single_filter = true;
   ESP_ERROR_CHECK(twai_driver_install(&global_config, &timing_config, &filter_config));
   ESP_ERROR_CHECK(twai_start());
   ESP_LOGI(TAG,"CAN init success :)");
   return ESP_OK;
}

void CAN_ritual(){
    twai_message_t CAN_frame;

    uint32_t ID;

    while(1){
        CAN_frame.identifier = 0x00;

        if(twai_receive(&CAN_frame,pdMS_TO_TICKS(1000)) != ESP_OK){ 
            //SP_LOGE(TAG,"No CAN was recieved looping again");
            LOOP_AGAIN_MOTHERFUCKER;
        }
        ID = CAN_frame.identifier % MAX_CAN_ID_COUNT;
        ESP_LOGI(TAG,"the id: %ld",ID);
        if(NULL == CAN_IDs_list[ID]) LOOP_AGAIN_MOTHERFUCKER;

        if(ID == global_time_ID){
            data_service_write_global_time(CAN_frame.data);
        }
        else{
            parse_and_store_frame(CAN_IDs_list[ID], CAN_frame.data);
        }
    }
}

void parse_and_store_frame(data_value_t* head, uint8_t* frame){
    uint64_t data_block;
    
    do{
        for(int i = 7;i>=0;i--){
            data_block |= (uint64_t)frame[i] << 8*(7-i); 
        }
        ESP_LOGI(TAG,"data: %lld",data_block);
        ESP_LOGI(TAG,"offset: %d | length: %d",head->offset,head->length);
        data_block = data_block << head->offset;
        data_block = data_block >> (64 - (head->length));
        ESP_LOGI(TAG,"data: %lld",data_block);
        data_service_write(head->index,(uint32_t)data_block);
        head = head->next;
    }while(head != NULL);
    return;
}