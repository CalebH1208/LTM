#include "LTM_LoRa.h"
#include "LEDs.h"
#include "LTM_data_service.h"

static const char *TAG = "LTM_LoRa";

// get the number of ticks that occur in 5 seconds
const TickType_t ticks_till_next_assignment = portTICK_PERIOD_MS * 10000; // maybe higher?

static spi_device_handle_t spi;

LoRaModule * LoRa;

uint64_t channel = 0;

int32_t carNum = -1;

int LoRa_Init(spi_config_t* spiConfig, LoRa_config_t* LoRaConfig){

    LoRa = LoRa_initialize(spiConfig->mosi,spiConfig->miso,spiConfig->clk,spiConfig->nss,spiConfig->clockspeed,spiConfig->host,&spi,NULL);
    ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_SLEEP, LoRa));
    ESP_ERROR_CHECK(LoRa_set_frequency(LoRaConfig->frequency,LoRa));
    ESP_ERROR_CHECK(LoRa_reset_fifo(LoRa));
    ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_STANDBY,LoRa));
    ESP_ERROR_CHECK(LoRa_set_bandwidth(LoRa, LoRaConfig->BW));
    ESP_ERROR_CHECK(LoRa_set_implicit_header(NULL, LoRa));
    ESP_ERROR_CHECK(LoRa_set_modem_config_2(LoRaConfig->SF,LoRa));
    ESP_ERROR_CHECK(LoRa_set_syncword(0xf3,LoRa));
    ESP_ERROR_CHECK(LoRa_set_preamble_length(8,LoRa));
    ESP_ERROR_CHECK(LoRa_tx_set_pa_config(LoRa_PA_PIN_BOOST,20,LoRa));

    LoRa_tx_header_t header = {
        .enable_crc = true,
        .coding_rate = LoRaConfig->CR
    };
    ESP_ERROR_CHECK(LoRa_tx_set_explicit_header(&header,LoRa));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    setup_gpio_interrupts(LoRaConfig->DIO0, LoRa, GPIO_INTR_POSEDGE);
    LoRa_rx_set_callback(LoRa_car_rx_callback,LoRa);
    ESP_ERROR_CHECK(setup_rx_task(LoRa));
    LoRa->Activity_LED = LoRaConfig->Activity_LED;
    ESP_LOGI(TAG,"LoRa init success :))");
    return ESP_OK;
}

int LoRa_Tx(uint8_t* data, int length){
    LED_on(LoRa->Activity_LED);
    ESP_ERROR_CHECK(LoRa_tx_set_for_transmission(data, length*8, LoRa));
    ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_TX, LoRa));
    LED_off(LoRa->Activity_LED);
    return ESP_OK;
}

int LoRa_ritual(){
    TickType_t last_assignment = xTaskGetTickCount();
    TickType_t curr_ticks;
    while(1){ //lol
        curr_ticks = xTaskGetTickCount();
        if((curr_ticks - last_assignment) > ticks_till_next_assignment || 0 == channel){
            channel = 0;
            LoRa_get_assigned_channel();
            last_assignment = xTaskGetTickCount();
        }
        uint8_t data[MAX_MSG_LEN];
        uint16_t len;
        esp_err_t err = data_service_get_LoRa_data(data, &len);
        if(err == ESP_OK)LoRa_Tx(data,len);
        xTaskDelayUntil(&curr_ticks,pdMS_TO_TICKS(1000/SEND_FREQUENCY)); // convert send HZ to delay needed
    }
}

int LoRa_get_assigned_channel(){
    if(carNum < 1){
        return ESP_ERR_INVALID_STATE;
    }
    while(0 == channel){
        LoRa_swap_channel(CHECK_IN_FREQ);
        LoRa_Tx((uint8_t *)&carNum,sizeof(int));
        
        ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_RX_CONT,LoRa));
        vTaskDelay(pdMS_TO_TICKS(1000)); // maybe decrease this?
    }
    LoRa_swap_channel(channel);
    return ESP_OK;
}

void LoRa_car_rx_callback(LoRaModule* chip ,uint8_t* data,uint16_t length){
    LED_on(LoRa->Activity_LED);
    int recieved_car_num;
    uint64_t temp_freq;
    for(int i =0; i<sizeof(int) && i < length;i++){
        recieved_car_num = recieved_car_num << 8 | data[i];
    }
    if(recieved_car_num != carNum)return;
    for(int i = sizeof(int); i < sizeof(int) + sizeof(uint64_t) && i < length; i++){
        temp_freq = temp_freq << 8 | data[i];
    }
    if(temp_freq < 900000000 || temp_freq > 915000000)return;
    channel = temp_freq;
    ESP_LOGI(TAG,"frequency set to %llu",channel);
    LED_off(LoRa->Activity_LED);
}

int LoRa_swap_channel(uint64_t frequency){
    esp_err_t err = LoRa_set_mode(LoRa_MODE_STANDBY,LoRa);
    if(err != ESP_OK)return err;
    err = LoRa_set_frequency(frequency,LoRa);
    return err;
}

void set_car_num(int number){
    carNum = number;
}