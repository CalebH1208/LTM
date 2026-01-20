#include "esp_task_wdt.h"

#include "shared.h"

#include "LTM_init.h"
#include "LTM_CAN.h"
#include "LTM_serial_service.h"
#include "LTM_espnow.h"

// #define PRINT_RUN_STATS 1

#define ActivityLED 5

#define CANTX 9
#define CANRX 14

static const char *TAG = "LTM Main";

void app_main(void){
    ESP_LOGI(TAG,"Size of %d",sizeof(float));

    LTM_type_t LTM_type;
    valid_CAN_speeds_t CAN_speed;
    car_state_t car_state;
    paddock_array_t paddock_array;
    int global_time;
    CAN_metadata_t can_metadata;
    uint32_t* LoRa_array;
    uint32_t LoRa_array_len = 0;
    data_value_t** CAN_ID_array;

    // ESP-NOW configuration
    uint8_t espnow_channel = 11;                        // Default WiFi channel
    uint8_t espnow_peer_macs[20][6] = {0};             // Max 20 peers
    uint8_t espnow_peer_count = 0;
    bool espnow_long_range = true;                      // Enable 802.11 LR mode

    espnow_config_t espnow_config = {
        .wifi_mode = WIFI_MODE_STA,
        .channel = espnow_channel,
        .long_range_mode = espnow_long_range,
        .activity_led = (gpio_num_t)ActivityLED,
    };

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
    initialization_parameters.espnow_channel = &espnow_channel;
    initialization_parameters.espnow_peer_macs = espnow_peer_macs;
    initialization_parameters.espnow_peer_count = &espnow_peer_count;
    initialization_parameters.espnow_long_range = &espnow_long_range;

    sdmmc_slot_config_t SD = SDMMC_SLOT_CONFIG_DEFAULT();
    SD.width = SD_BUS_WIDTH;
    SD.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    SD.d0 = 37;
    SD.clk = 36;
    SD.cmd = 35;

    ESP_ERROR_CHECK(SD_init(&SD));
    ESP_ERROR_CHECK(parse_JSON_globals(initialization_parameters));

    // Configure watchdog timer with extended timeout for long printf operations
    // Default is 5 seconds, increase to 15 seconds to prevent timeout during serial output
    ESP_LOGI(TAG, "Configuring task watchdog timer (15 second timeout)");
    ESP_ERROR_CHECK(esp_task_wdt_deinit());  // Deinit default watchdog first

    // Configure watchdog with 15 second timeout (ESP-IDF v5.4 API)
    esp_task_wdt_config_t wdt_config = {
            .timeout_ms = 150000,           
        .idle_core_mask = (1 << 0) | (1 << 1),  // Monitor both CPU cores
        .trigger_panic = false          // Panic on timeout
    };
    ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
    ESP_LOGI(TAG, "Task watchdog configured: 15s timeout, monitoring IDLE tasks on both cores");

    if(LTM_type == CAR){
        ESP_LOGI(TAG,"THIS IS A CAR SIDE ESP");
        ESP_ERROR_CHECK(write_header());
        ESP_ERROR_CHECK(finish_initialization());

        ESP_ERROR_CHECK(data_service_init(&car_state, LoRa_array,LoRa_array_len));
        ESP_ERROR_CHECK(CAN_init((gpio_num_t)CANTX, (gpio_num_t)CANRX, CAN_speed, CAN_ID_array,global_time));
        ESP_ERROR_CHECK(data_logging_init(can_metadata));
        ESP_ERROR_CHECK(espnow_init(&espnow_config, car_state.car_number, LTM_type,
                                    espnow_peer_macs, espnow_peer_count));
        ESP_LOGI(TAG, "inits ran");

        xTaskCreatePinnedToCore(CAN_ritual,"CAN ritual",4096,NULL,2,NULL,0);
        xTaskCreatePinnedToCore(data_logging_ritual,"Datalogging ritual",16384,NULL,1,NULL,1);
        xTaskCreate(espnow_car_ritual,"ESP-NOW ritual",8192,NULL,1,NULL);
        ESP_LOGI(TAG, "rituals ran");
    }
    else{
        ESP_LOGI(TAG,"THIS IS THE PADDOCK SIDE ESP");
        ESP_ERROR_CHECK(serial_service_init(&paddock_array));
        ESP_ERROR_CHECK(espnow_init(&espnow_config, 0, LTM_type,
                                    espnow_peer_macs, espnow_peer_count));
        ESP_LOGI(TAG, "inits ran");

        xTaskCreate(espnow_paddock_ritual, "ESP-NOW Ritual", 16384, NULL, 2, NULL);
        ESP_LOGI(TAG, "rituals ran");
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
    while(1) {vTaskDelay(pdMS_TO_TICKS(100000));}
}
