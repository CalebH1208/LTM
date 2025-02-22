#include "LTM_init.h"

init_struct_t* init_structure;

static const signed char *TAG = "LTM init";

sdmmc_card_t* = SD_card;

uint32_t * indices_100Hz;
uint32_t length_100Hz;

uint32_t * indices_10Hz;
uint32_t length_10Hz;

uint32_t * indices_1Hz;
uint32_t length_1Hz;

esp_err_t SD_init(sdmmc_slot_config_t* config){
    const char mountpoint[] = MOUNT_POINT;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = {
        .cmd = gpio_num_35,
        .clk = gpio_num_36,
        .d0 = gpio_num_37,
        .d1 = gpio_num_38,
        .d2 = gpio_num_1,
        .d3 = gpio_num_2
    }
    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG()
    mount_config.max_files = 5,
    mount_config.format_if_mount_failed = false

    ESP_ERROR_CHECK(esp_vfs_fat_sdmmc_mount(mountpoint,&host,&slot,&mount_config,&SD_card));
    ESP_LOGI(TAG, "SD card has been initialized");
}

esp_err_t parse_JSON_globals(valid_CAN_speeds_t* bus_speed, data_value_t*** CAN_ID_array, car_state_t* car_state){

}

esp_err_t parse_JSON_CAN(CAN_metadata_t* can_data, car_state_t* car_state, uint32_t** LoRa_array, uint32_t* LoRa_array_length){

}

esp_err_t write_header(){
    FILE * fptr_100HZ = fopen(PATH_100HZ, "a");
    FILE * fptr_10HZ = fopen(PATH_10HZ, "a");
    FILE * fptr_1HZ = fopen(PATH_1HZ, "a");

    ESP_ERROR_CHECK(write_header_helper(fptr_100HZ,indices_100Hz,length_100Hz));
    ESP_ERROR_CHECK(write_header_helper(fptr_10HZ,indices_10Hz,length_10Hz));
    ESP_ERROR_CHECK(write_header_helper(fptr_1HZ,indices_1Hz,length_1Hz));
    
    return ESP_OK;
}

esp_err_t write_header_helper(FILE* file,uint32_t * indices, uint32_t num_indices){
    char * buffer = calloc(BUFFER_LENGTH, sizeof(char));
    if(NULL == buffer){
        return ESP_ERR_NO_MEM;
    }
    size_t bytes_written = 0;
    size_t buffer_index = 0;
    bytes_written = snprintf(buffer, BUFFER_LENGTH, "Time, Global Time");
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    for(int i = 0; i < num_indices; i++){
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%s", init_structure[indices[i]]->name);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    buffer[++buffer_index] = '\0';
    write_data(buffer,++buffer_index,file);
    buffer_index = 0;
    bytes_written = 0;

    bytes_written = snprintf(buffer, BUFFER_LENGTH, "µsec, lmao");
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    for(int i = 0; i < num_indices; i++){
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%s", init_structure[indices[i]]->unit);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    buffer[++buffer_index] = '\0';
    write_data(buffer,++buffer_index,file);
    buffer_index = 0;
    bytes_written = 0;

    bytes_written = snprintf(buffer, BUFFER_LENGTH, "1, 1");
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    for(int i = 0; i < num_indices; i++){
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%.7f", init_structure[indices[i]]->conversion);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    buffer[++buffer_index] = '\0';
    write_data(buffer,++buffer_index,file);
    buffer_index = 0;
    bytes_written = 0;

    bytes_written = snprintf(buffer, BUFFER_LENGTH, "1000000, 1");
    if(bytes_written > 0){
        buffer_index += bytes_written;
    }else{
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    for(int i = 0; i < num_indices; i++){
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%d", init_structure[indices[i]]->precision);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    buffer[++buffer_index] = '\0';
    write_data(buffer,++buffer_index,file);
    buffer_index = 0;
    bytes_written = 0;
    return ESP_OK;
}
