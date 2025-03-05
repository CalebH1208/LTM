#include "esp_task_wdt.h"

#include "shared.h"

#include "LTM_init.h"
#include "LTM_CAN.h"

bool is_sender = false;

#define PRINT_RUN_STATS 1

#define LoRaLED 5

#define NSS 10
#define MOSI 11
#define SPI_CLK 12
#define MISO 13
#define RST 48
#define DIO0PIN 21
#define DIO1 47

#define CMD 39
#define SD_CLK 16
#define D0 17
#define D1 38
#define D2 1
#define D3 2

#define CANTX 9
#define CANRX 14

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
    LTM_type_t LTM_type;
    valid_CAN_speeds_t CAN_speed;
    car_state_t car_state;
    paddock_array_t paddock_array;
    int global_time;
    CAN_metadata_t can_metadata;
    uint32_t* LoRa_array;
    uint32_t LoRa_array_len;
    data_value_t** CAN_ID_array;

    init_parameters initialization_parameters;
    initialization_parameters.LTM_type = &LTM_type;
    initialization_parameters.bus_speed = &CAN_speed;
    initialization_parameters.car_state = &car_state;
    initialization_parameters.paddock_array = &paddock_array;
    initialization_parameters.global_time_id = &global_time;
    initialization_parameters.can_data = &can_metadata;
    initialization_parameters.LoRa_array = &LoRa_array;
    initialization_parameters.LoRa_array_length = &LoRa_array_len;
    initialization_parameters.CAN_ID_array = &CAN_ID_array;

    spi_config_t spi ={
        .miso = MISO,
        .mosi = MOSI,
        .clk = SPI_CLK,
        .clockspeed = SPI_SPEED,
        .nss = NSS,
        .host = SPI3_HOST,
    };

    LoRa_config_t LoRa = {
        .Activity_LED = LoRaLED,
        .BW = LoRa_BW_500000,
        .CR = LoRa_CR_4_5,
        .SF = LoRa_SF_7,
        .DIO0 = DIO0PIN,
        .frequency = LORA_DEFAULT_FREQUENCY,
    };

    sdmmc_slot_config_t SD = SDMMC_SLOT_CONFIG_DEFAULT();
    SD.width = SD_BUS_WIDTH;
    SD.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    SD.d0 = 37;
    SD.clk = 36;
    SD.cmd = 35;

    ESP_ERROR_CHECK(SD_init(&SD));
    ESP_ERROR_CHECK(parse_JSON_globals(initialization_parameters));
    if(LTM_type == CAR){
        ESP_ERROR_CHECK(write_header());
        ESP_ERROR_CHECK(finish_initialization());
        //ESP_LOGI(TAG,"LENGTHS: %ld | %ld | %ld\n",can_metadata.indices_100Hz_p[0],can_metadata.indices_10Hz_p[0], can_metadata.indices_10Hz_p[1]);
        
        ESP_ERROR_CHECK(data_service_init(&car_state, LoRa_array,LoRa_array_len));
        ESP_ERROR_CHECK(CAN_init((gpio_num_t)CANTX, (gpio_num_t)CANRX, CAN_speed, CAN_ID_array,global_time));
        ESP_ERROR_CHECK(data_logging_init(can_metadata));
        ESP_ERROR_CHECK(LoRa_Init(&spi,&LoRa,car_state.car_number));
        ESP_LOGI(TAG, "inits ran");

        xTaskCreatePinnedToCore(CAN_ritual,"CAN ritual",4096,NULL,2,NULL,0);
        xTaskCreatePinnedToCore(data_logging_ritual,"Datalogging ritual",16384,NULL,1,NULL,1);
        xTaskCreatePinnedToCore(LoRa_ritual,"LoRa ritual",8192,NULL,2,NULL,0);
        ESP_LOGI(TAG, "rituals ran");
    }
    else{
        ESP_ERROR_CHECK(data_service_init_paddock(paddock_array));
        ESP_ERROR_CHECK(LoRa_Init(&spi,&LoRa,car_state.car_number));
    }

#ifdef PRINT_RUN_STATS
    char buffer[512] = {0};
    while(1){
        vTaskGetRunTimeStats(buffer);
        buffer[512] = '\0';
        vTaskDelay(pdMS_TO_TICKS(5000));
        printf("%s\n",buffer);
    }
#endif
}
