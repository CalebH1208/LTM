#include "esp_task_wdt.h"

#include "shared.h"

#include "LTM_init.h"

bool is_sender = false;

#define LoRaLED 5

#define NSS 10
#define MOSI 11
#define CLK 12
#define MISO 13
#define RST 48
#define DIO0PIN 21
#define DIO1 47

#define CANTX //fix me
#define CANRX // fix me

static const char *TAG = "LTM Main";

void app_main(void){
    /*
    typedef struct {
    valid_CAN_speeds_t* bus_speed;
    car_state_t* car_state;
    int* global_time_id;
    CAN_metadata_t* can_data;
    uint32_t** LoRa_array;
    uint32_t* LoRa_array_length;
    data_value_t*** CAN_ID_array;
    }init_parameters;
    */
    valid_CAN_speeds_t CAN_speed;
    car_state_t car_state;
    int global_time;
    CAN_metadata_t can_metadata;
    uint32_t* LoRa_array;
    uint32_t LoRa_array_len;
    data_value_t** CAN_ID_array;

    init_parameters initialization_parameters;
    initialization_parameters.bus_speed = &CAN_speed;
    initialization_parameters.car_state = &car_state;
    initialization_parameters.global_time_id = &global_time;
    initialization_parameters.can_data = &can_metadata;
    initialization_parameters.LoRa_array = &LoRa_array;
    initialization_parameters.LoRa_array_length = &LoRa_array_len;
    initialization_parameters.CAN_ID_array = &CAN_ID_array;

    spi_config_t spi ={
        .miso = MISO,
        .mosi = MOSI,
        .clk = CLK,
        .clockspeed = 10000000,
        .nss = NSS,
        .host = SPI3_HOST,
    };

    LoRa_config_t LoRa = {
        .Activity_LED = LoraLED,
        .BW = LoRa_BW_500000,
        .CR = LoRa_CR_4_5,
        .SF = LoRa_SF_7,
        .DIO0 = DIO0PIN,
        .frequency = 915000000,
    };

    ESP_ERROR_CHECK(parse_JSON_globals(initialization_parameters));
    ESP_ERROR_CHECK(write_header());
    
    ESP_ERROR_CHECK(data_service_init(&car_state, LoRa_array,LoRa_array_len));
    ESP_ERROR_CHECK(CAN_init(CANTX, CANRX, CAN_speed, CAN_ID_array));
    ESP_ERROR_CHECK(data_logging_init(can_metadata));
    ESP_ERROR_CHECK(LoRa_Init(&spi,&LoRa));


    xTaskCreatePinnedToCore(CAN_ritual,"CAN ritual",4096,NULL,2,NULL,1);
    xTaskCreatePinnedToCore(data_logging_ritual,"Datalogging ritual",16384,NULL,1,NULL,1);
    xTaskCreatePinnedToCore(LoRa_ritual,"LoRa ritual",8192,NULL,2,NULL,0);

}
