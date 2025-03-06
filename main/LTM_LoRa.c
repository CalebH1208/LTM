#include "LTM_LoRa.h"
#include "LEDs.h"
#include "LTM_data_service.h"

static const char *TAG = "LTM_LoRa";

static spi_device_handle_t spi;

LoRaModule * LoRa;

uint64_t channel = 0;

int car_num = -1;

channel_allocation_t channels[NUMBER_OF_CHANNELS];

static SemaphoreHandle_t rx_signal;
StaticSemaphore_t rx_signal_buffer;

int LoRa_Init(spi_config_t* spiConfig, LoRa_config_t* LoRaConfig, uint32_t car){
    car_num = car;

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

void LoRa_ritual(){
    TickType_t last_assignment = xTaskGetTickCount();
    TickType_t curr_ticks;
    LoRa_rx_set_callback(LoRa_car_rx_callback,LoRa);
    ESP_ERROR_CHECK(setup_rx_task(LoRa));
    while(1){ //lol
        curr_ticks = xTaskGetTickCount();
        //printf("%ld | %ld | %d\n",curr_ticks , last_assignment, (curr_ticks - last_assignment) > TICKS_TILL_NEXT_ASSIGNMENT);
        if((curr_ticks - last_assignment) > TICKS_TILL_NEXT_ASSIGNMENT || 0 == channel){
            channel = 0;
            LoRa_get_assigned_channel();
            last_assignment = xTaskGetTickCount();
        }
        uint8_t data[MAX_MSG_LEN];
        uint16_t len;
        esp_err_t err = data_service_get_LoRa_data(data, &len, car_num);
        //ESP_LOGI(TAG,"Sending: %lld",channel);
        if(err == ESP_OK)LoRa_Tx(data,len);
        xTaskDelayUntil(&curr_ticks,pdMS_TO_TICKS(1000/SEND_FREQUENCY)); // convert send HZ to delay needed
    }
}

int LoRa_get_assigned_channel(){
    if(car_num < 1){
        return ESP_ERR_INVALID_STATE;
    }
    while(0 == channel){
        LoRa_swap_channel(CHECK_IN_FREQ);
        LoRa_Tx((uint8_t *)&car_num,sizeof(int));
        //ESP_LOGI(TAG,"car num sent: %d",car_num);
        ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_RX_CONT,LoRa));
        vTaskDelay(pdMS_TO_TICKS(SEND_CHANNEL_REQUEST_PERIOD)); // maybe decrease this?
    }
    LoRa_swap_channel(channel);
    return ESP_OK;
}

void LoRa_car_rx_callback(LoRaModule* chip, uint8_t* data, uint16_t length){
    LED_on(LoRa->Activity_LED);
    int recieved_car_num = -1;
    uint64_t temp_freq = 0;
    for(int i = sizeof(int)-1; i >= 0 && i < length; i--){
        recieved_car_num = recieved_car_num << 8 | data[i];
    }
    //printf("%d\n",recieved_car_num);
    if(recieved_car_num != car_num)return;
    for(int i = 2*sizeof(int) - 1; i >= sizeof(int) && i < length; i--){
        temp_freq = temp_freq << 8 | data[i];
    }
    temp_freq = CHECK_IN_FREQ - (temp_freq * CHANNEL_STEP);
    ESP_LOGI(TAG,"frequency recieved: %lld",temp_freq);
    if(temp_freq < CHECK_IN_FREQ - (CHANNEL_STEP*NUMBER_OF_CHANNELS) || temp_freq > CHECK_IN_FREQ) return;
    channel = temp_freq;
    ESP_LOGI(TAG,"frequency set to %llu",channel);
    LED_off(LoRa->Activity_LED);
}

int LoRa_swap_channel(uint64_t frequency){
    esp_err_t err = LoRa_set_mode(LoRa_MODE_SLEEP,LoRa);
    if(err != ESP_OK)return err;
    err = LoRa_set_frequency(frequency,LoRa);
    //printf("%d\n",err);
    return err;
}

void LoRa_paddock_ritual(){
    rx_signal = xSemaphoreCreateBinaryStatic(&rx_signal_buffer);
    LoRa_rx_set_callback(LoRa_paddock_rx_callback,LoRa);
    ESP_ERROR_CHECK(setup_rx_task(LoRa));
    for(int i = 1; i < NUMBER_OF_CHANNELS; i++){ // fun for loop :)
        channels[i].channel = i;
        channels[i].car_num = 0;
        channels[i].expiry = 0;
    }
    TickType_t curr_ticks;
    while(1){
        curr_ticks = xTaskGetTickCount();
        for(int i = 1; i < NUMBER_OF_CHANNELS;i++){
            //printf("%ld ,%ld, %d, i=%d\n",channels[i].expiry , curr_ticks, channels[i].expiry > curr_ticks,i);
            if(channels[i].expiry > curr_ticks){
                LoRa_swap_channel(CHECK_IN_FREQ - ((uint64_t)channels[i].channel * CHANNEL_STEP));
                channel = 1;
                xSemaphoreTake(rx_signal,0);
                LoRa_set_mode(LoRa_MODE_RX_CONT, LoRa); // might need to be CONT mode if the chip has short rx single wait period
                xSemaphoreTake(rx_signal,pdMS_TO_TICKS(CHANNEL_WAIT_TIME_MS));
            }
        }
        channel = 0;
        LoRa_swap_channel(CHECK_IN_FREQ);
        LoRa_set_mode(LoRa_MODE_RX_CONT, LoRa);
        vTaskDelay(pdMS_TO_TICKS(CHECK_IN_WAIT_TIME_MS)); // try making this smaller later :)
    }
}

void LoRa_paddock_rx_callback(LoRaModule* chip ,uint8_t* data, uint16_t length){
    LED_on(LoRa->Activity_LED);
    if(0 != channel){
        //printf("trying to print sent data: %d\n",length);
        xSemaphoreGive(rx_signal);
        serial_send(data, length);
    }
    else{
        //printf("trying to give out a channel\n");
        int recieved_car_num = -1;
        for(int i = sizeof(int)-1; i >= 0 && i < length; i--){
            recieved_car_num = recieved_car_num << 8 | data[i];
        }
        TickType_t curr_ticks = xTaskGetTickCount();
        channel_enum_t frequency = 0;
        bool found = false;
        for(int i = 1; i < NUMBER_OF_CHANNELS;i++){
            if(channels[i].car_num == recieved_car_num ){
                frequency = channels[i].channel;
                channels[i].car_num = recieved_car_num;
                channels[i].expiry = curr_ticks + TICKS_TILL_CHANNEL_EXPIRE;
                found = true;
                break;
            }
        }
        if(!found){
            for(int i = 1; i< NUMBER_OF_CHANNELS;i++){
                if(curr_ticks > channels[i].expiry ){
                    frequency = channels[i].channel;
                    channels[i].car_num = recieved_car_num;
                    channels[i].expiry = curr_ticks + TICKS_TILL_CHANNEL_EXPIRE;
                    break;
                }
            }
        }    
        if(0 == frequency)return;
        ESP_LOGI(TAG,"here is the num: %d | and the freq: %d",recieved_car_num,frequency);
        uint32_t data[2] = {recieved_car_num, frequency};
        LoRa_Tx((uint8_t*)data,2*sizeof(uint32_t));
    }
    LED_off(LoRa->Activity_LED);
}