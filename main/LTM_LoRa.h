#ifndef LTM_LORA_H
#define LTM_LORA_H
#include "shared.h"
#include "LTM_data_service.h"
#include "LTM_serial_service.h"
#include "LoRa.h"

#define MAX_MSG_LEN 256

#define CHECK_IN_FREQ 915000000

#define SEND_FREQUENCY 50

#define SEND_CHANNEL_REQUEST_PERIOD 100

#define NUMBER_OF_CHANNELS 9

#define CHANNEL_STEP 1500000

#define CHANNEL_WAIT_TIME_MS 30


// get the number of ticks that occur in 30 seconds
 // maybe higher?
#define TICKS_TILL_NEXT_ASSIGNMENT portTICK_PERIOD_MS*30000

#define TICKS_TILL_CHANNEL_EXPIRE TICKS_TILL_NEXT_ASSIGNMENT+1000*portTICK_PERIOD_MS

typedef struct{
  uint8_t car_num;
  uint64_t channel;
  TickType_t expiry;
}channel_allocation_t;

typedef struct {
    int mosi;
    int miso;
    int clk;
    int nss;
    uint32_t clockspeed;
    spi_host_device_t host;
  }spi_config_t;
  
  typedef struct {
    LoRa_bw_t BW;
    LoRa_sf_t SF;
    LoRa_cr_t CR;
    uint64_t frequency;
    gpio_num_t DIO0;
    gpio_num_t Activity_LED;
  }LoRa_config_t;

int LoRa_Init(spi_config_t* spiConfig, LoRa_config_t* LoRaConfig,uint32_t car_num);

int LoRa_Tx(uint8_t* data, int length);

int LoRa_get_assigned_channel();

void LoRa_ritual();

void LoRa_paddock_ritual();

void LoRa_car_rx_callback(LoRaModule* chip ,uint8_t* data,uint16_t length);

void LoRa_paddock_rx_callback(LoRaModule* chip ,uint8_t* data, uint16_t length);

int LoRa_swap_channel(uint64_t frequency);

#endif //LTM_LORA_H