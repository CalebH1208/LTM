#ifndef LTM_LORA_H
#define LTM_LORA_H
#include "shared.h"
#include "LTM_data_service.h"
#include "LoRa.h"

#define MAX_MSG_LEN 256

#define CHECK_IN_FREQ 915000000

#define SEND_FREQUENCY 20

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

int LoRa_Init(spi_config_t* spiConfig, LoRa_config_t* LoRaConfig);

int LoRa_Tx(uint8_t* data, int length);

int LoRa_get_assigned_channel();

int LoRa_car_ritual();

int LoRa_paddck_ritual();

void LoRa_car_rx_callback(LoRaModule* chip ,uint8_t* data,uint16_t length);

int LoRa_swap_channel(uint64_t frequency);

void set_car_num(int number);

#endif //LTM_LORA_H