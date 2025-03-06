#ifndef SPI_H
#define SPI_H

#include "driver/spi_master.h"
#include "esp_log.h"

int spi_initialize_device(spi_bus_config_t* config, spi_device_interface_config_t* device,spi_host_device_t host,spi_device_handle_t* handle,void** DMA);

int Spi_write_register(spi_device_handle_t* device,int register,uint8_t* data,size_t length);

int Spi_write_buffer(spi_device_handle_t* device,int register,uint8_t* buffer,size_t bufferLength);

int Spi_read_register(spi_device_handle_t* device, int register,size_t dataLength, uint32_t *result);

int Spi_read_buffer(spi_device_handle_t* device, int register,size_t bufferLength, uint8_t *buffer);

int Spi_append_register(spi_device_handle_t* device,int register,uint8_t mask,uint8_t value);

#endif //SPI_H