#include "Spi.h"

static const char *TAG = "SPI";

int spi_initialize_device(spi_bus_config_t* config, spi_device_interface_config_t* device,spi_host_device_t host,spi_device_handle_t* handle,void** DMA){
    esp_err_t check = spi_bus_initialize(host,config,SPI_DMA_CH_AUTO);
    if(check != ESP_OK){
        ESP_LOGE(TAG,"Spi initialization Error");
        return check;
    }
    // attempt to allocate DMA
    // if(DMA == NULL){
    //     *DMA = spi_bus_dma_memory_alloc(host,256,32);
    //     if(*DMA == NULL){
    //         ESP_LOGE(TAG, "DMA allocation failed");
    //     }
    // }

    //create device
    check = spi_bus_add_device(host,device,handle);
    if(check != ESP_OK){
        ESP_LOGE(TAG,"Device initialization Error");
        return check;
    }
    return ESP_OK;
}

int Spi_write_register(spi_device_handle_t* device,int reg,uint8_t* data,size_t length){
    if (length == 0 || length > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t msg ={
        .addr = reg | 0x80,
        .rx_buffer = NULL,
        .tx_buffer = NULL,
        .rxlength = length * 8,
        .length = length * 8,
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA
    };
    for (int i = 0; i < length; i++) {
        msg.tx_data[i] = data[i];
    }
    return spi_device_polling_transmit(*device, &msg);
}

int Spi_write_buffer(spi_device_handle_t* device,int reg,uint8_t* buffer,size_t bufferLength){
     spi_transaction_t msg ={
        .addr = reg | 0x80,
        .rx_buffer = NULL,
        .tx_buffer = buffer,
        .rxlength = bufferLength * 8,
        .length = bufferLength * 8,
    };
    return spi_device_polling_transmit(*device, &msg);
}

int Spi_read_register(spi_device_handle_t* device, int reg,size_t dataLength, uint32_t *result){
    if (dataLength == 0 || dataLength > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = 0;
    spi_transaction_t msg = {
      .addr = reg & 0x7F,
      .rx_buffer = NULL,
      .tx_buffer = NULL,
      .rxlength = dataLength * 8,
      .length = dataLength * 8,
      .flags = SPI_TRANS_USE_RXDATA
    };

    // static SemaphoreHandle_t spi_mutex = NULL;
    // spi_mutex = xSemaphoreCreateMutex();
    // xSemaphoreTake(spi_mutex, portMAX_DELAY);

    esp_err_t code = spi_device_transmit(*device, &msg);

    //xSemaphoreGive(spi_mutex);

    if (code != ESP_OK) {
        return code;
    } 
    for (int i = 0; i < dataLength; i++) {
        *result = ((*result) << 8);
        *result = (*result) + msg.rx_data[i];
    }
    return ESP_OK;
}

int Spi_read_buffer(spi_device_handle_t* device, int reg,size_t bufferLength, uint8_t *buffer){
    spi_transaction_t msg ={
        .addr = reg & 0x7F,
        .rx_buffer = buffer,
        .tx_buffer = NULL,
        .rxlength = bufferLength * 8,
        .length = bufferLength * 8
    };
    return spi_device_polling_transmit(*device, &msg);
}

int Spi_append_register(spi_device_handle_t* device,int reg,uint8_t mask,uint8_t value){
    uint8_t old = 0;
    Spi_read_register(device,reg,1,(uint32_t*)&old);
    uint8_t data[] = {(old & mask) | value};
    return Spi_write_register(device,reg,data,1);
}