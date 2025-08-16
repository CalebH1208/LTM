#include "LTM_LoRa.h"
#include "LEDs.h"
#include "LTM_data_service.h"

static const char *TAG = "LTM_LoRa";

static spi_device_handle_t spi;

LoRaModule * LoRa;

uint64_t channel = 0;

int car_num = -1;

uint64_t* channels;

static SemaphoreHandle_t rx_signal;
StaticSemaphore_t rx_signal_buffer;

int LoRa_Init(spi_config_t* spiConfig, LoRa_config_t* LoRaConfig, uint32_t car, uint64_t* chan){
    car_num = car;
    channels = chan;
    channel =  LoRaConfig->frequency;
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
    ESP_ERROR_CHECK(LoRa_tx_set_pa_config(LoRa_PA_PIN_BOOST,10,LoRa));

    LoRa_tx_header_t header = {
        .enable_crc = true,
        .coding_rate = LoRaConfig->CR
    };
    ESP_ERROR_CHECK(LoRa_tx_set_explicit_header(&header,LoRa));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    setup_gpio_interrupts(LoRaConfig->DIO0, LoRa, GPIO_INTR_POSEDGE);
    LoRa->Activity_LED = LoRaConfig->Activity_LED;
    ESP_LOGI(TAG,"LoRa init success :))");
    return ESP_OK;
}

int LoRa_Tx(uint8_t* data, int length){
    LED_on(LoRa->Activity_LED);
    ESP_ERROR_CHECK(LoRa_tx_set_for_transmission(data, length*8, LoRa));
    ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_TX, LoRa));
    // delay_parameters delay = {
    //     .gpio = LoRa->Activity_LED,
    //     .delayMS = 10
    // };
    // xTaskCreate(LED_delayed_off,"LoRa_LED_off",512,(void*)&delay,1,NULL);
    //LED_off(LoRa->Activity_LED);
    return ESP_OK;
}

void LoRa_car_ritual(){
    TickType_t curr_ticks;
    while(1){ //lol
        if(channel < 900000000 || channel > 915000000){
            LOOP_AGAIN_MOTHERFUCKER;
        }
        curr_ticks = xTaskGetTickCount();
        
        //printf("%ld | %ld | %d\n",curr_ticks , last_assignment, (curr_ticks - last_assignment) > TICKS_TILL_NEXT_ASSIGNMENT);
        // if((curr_ticks - last_assignment) > TICKS_TILL_NEXT_ASSIGNMENT || 0 == channel){
        //     channel = 0;
        //     LoRa_get_assigned_channel();
        //     last_assignment = xTaskGetTickCount();
        // }
        uint8_t data[MAX_MSG_LEN];
        uint16_t len;
        esp_err_t err = data_service_get_LoRa_data(data, &len, car_num);
        //ESP_LOGI(TAG,"Sending: %lld",channel);
        if(err == ESP_OK)LoRa_Tx(data,len);
        xTaskDelayUntil(&curr_ticks,pdMS_TO_TICKS(1000/SEND_FREQUENCY)); // convert send HZ to delay needed
    }
}

int LoRa_swap_channel(uint64_t frequency){
    esp_err_t err = LoRa_set_mode(LoRa_MODE_SLEEP,LoRa);
    if(err != ESP_OK)return err;
    err = LoRa_set_frequency(frequency,LoRa);
    return err;
}

void LoRa_paddock_ritual(){
    rx_signal = xSemaphoreCreateBinaryStatic(&rx_signal_buffer);
    LoRa_rx_set_callback(LoRa_paddock_rx_callback,LoRa);
    ESP_ERROR_CHECK(setup_rx_task(LoRa));
    LoRa_swap_channel(channels[0]);
    LoRa_set_mode(LoRa_MODE_RX_CONT, LoRa);
    int channel_index = 0;
    while(1){
        //ESP_LOGI(TAG, "Swapping channels to: %lld, %d", channels[channel_index], channel_index);
        LoRa_swap_channel(channels[channel_index]);
        LoRa_set_mode(LoRa_MODE_RX_CONT, LoRa);
        xSemaphoreTake(rx_signal,pdMS_TO_TICKS(500));
        channel_index++;
        if(channels[channel_index] == 0){
            channel_index = 0;
        }
        
        // add the hopping bullshit here for reception
    }
}

void LoRa_paddock_rx_callback(LoRaModule* chip ,uint8_t* data, uint16_t length){
    LED_on(LoRa->Activity_LED);
    xSemaphoreGive(rx_signal);
    serial_send(data, length);
    LED_off(LoRa->Activity_LED);
}