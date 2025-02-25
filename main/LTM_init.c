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
    return ESP_OK;
}


// honestly please just make this an initialization struct cause holy shit is this too long
esp_err_t parse_JSON_globals(init_parameters params){
    valid_CAN_speeds_t* bus_speed = params.bus_speed;
    car_state_t* car_state = params.car_state;
    int* global_time_id = params.global_time_id;
    CAN_metadata_t* can_data = params.can_data;
    uint32_t** LoRa_array = params.LoRa_array;
    uint32_t* LoRa_array_length = params.LoRa_array_length;
    data_value_t** CAN_ID_array = params.CAN_ID_array;
    
    char config_file_path[] = MOUNT_POINT"config.txt";
    FILE* config_file = fopen(config_file_path,"r"); // might need to use "rb" also this is why .json files dont work :)
    if(NULL == config_file)return ESP_ERR_NO_MEM;

    fseek(config_file, 0, SEEK_END);
    uint32_t length = ftell(config_file);
    fseek(config_file, 0, SEEK_SET);

    char* json_data = (char*)malloc((length+1) * sizeof(char));
    if(NULL == json_data){
        fclose(config_file);
        return ESP_ERR_NO_MEM;
    }

    fread(json_data, sizeof(char),length,config_file);
    json_data[length] = '\0';
    fclose(config_file);

    cJson *root = cJSON_Parse(json_data);
    if(NULL == root){
        free(json_data);
        return ESP_ERR_NOT_FINISHED;
    }

    cJSON* JSON_bus_speed = cJSON_GetObjectItem(root,"BS");
    if (!cJSON_IsNumber(JSON_bus_speed))return ESP_ERR_INVALID_STATE;
    switch(JSON_bus_speed.valueint){
        case(1000000):
            *bus_speed = CAN_1MBITS;
            break;
        case(500000):
            *bus_speed = CAN_500KBITS;
            break;
        case(250000):
            *bus_speed = CAN_250KBITS;
            break;
        case(100000):
            *bus_speed = CAN_100KBITS;
            break;
        case(50000):
            *bus_speed = CAN_50KBITS;
            break;
        default:
            *bus_speed = CAN_1MBITS;
    }

    cJSON* car_num = cJSON_GetObjectItem(root, "CN");
    cJSON* GID = cJSON_GetObjectItem(root, "GID");

    if(!cJSON_IsNumber(car_num) || !cJSON_IsNumber(GID)){
        cJSON_Delete(root);
        free(json_data);
        return ESP_ERR_NOT_FINISHED;
    }
    car_state->car_number = car_num->valueint;
    *global_time_id = GID->valueint;
    ////////////////////////////////////////////////////// maybe exit here and use parse JSON can but for now its here feel free to seperate it justin
    cJSON* CAN_items = cJSON_GetObjectItem(root,"CAN");

    uint32_t array_len = cJSON_GetArraySize(CAN_items);
    init_structure = malloc(sizeof(init_struct_t) * array_len);
    car_state->data_length = array_len;
    car_state->elements = malloc(sizeof(car_element_t)* array_len);

    indices_100Hz = malloc(sizeof(uint32_t) * array_len);
    indices_10Hz = malloc(sizeof(uint32_t) * array_len);
    indices_1Hz = malloc(sizeof(uint32_t) * array_len);

    LoRa_array = malloc(sizeof(uint32_t) * array_len);

    CAN_ID_array = malloc(sizeof(data_value_t*) * MAX_CAN_ID_COUNT);

    if(NULL == car_state->elements){
        cJSON_Delete(root);
        free(json_data);
        return ESP_ERR_NO_MEM;
    }
    
    cJSON* interated_item = NULL;
    int index =0;
    cJSON_ArrayForEach(interated_item,CAN_items){
        cJson* conv = cJson_getobject(interated_item,"C"); // lmao this shit is cooked :)
        cJson* unit = cJson_getobject(interated_item,"U");
        cJson* name = cJson_getobject(interated_item,"N");
        cJson* type = cJson_getobject(interated_item,"T");
        cJson* prec = cJson_getobject(interated_item,"P");
        cJson* freq = cJson_getobject(interated_item,"F");
        cJson* LoRa = cJson_getobject(interated_item,"L");
        cJson* bitO = cJson_getobject(interated_item,"BO");
        cJson* bitL = cJson_getobject(interated_item,"BL");
        cJson* hxID = cJson_getobject(interated_item,"ID");
        init_structure[index].name = name->valuestring;
        init_structure[index].conversion =(float) conv->valuedouble;
        init_structure[index].unit = unit->valuestring;
        init_structure[index].precision = (float) prec->valuedouble;
        car_state->elements[index].type = type->valueint;
        car_state->elements[index].data.u = 0;

        switch(freq->valueint){
            case(100):
                indices_100Hz[length_100Hz] = index;
                length_100Hz++:
                break;
            case(10):
                indices_10Hz[length_10Hz] = index;
                length_10Hz++:
                break;
            case(1):
                indices_1Hz[length_1Hz] = index;
                length_1Hz++;
                break;
        };
        if(1 == LoRa->valueint){
            (*LoRa_array)[*LoRa_array_length] = index;
            (*LoRa_array_length)++;
        }

    // data_value_t*** CAN_ID_array
        int bit_offset = bitO->valueint;
        int bit_length = bitL->valueint;
        int ID = strtol(hxID->valuestring,NULL,16);
        data_value_t * can_item = malloc(sizeof(data_value_t));
        can_item->index = index;
        can_item->offset = bit_offset;
        can_item->length = bit_length;
        can_item->next = NULL;
        if(ID / 0x100 == 7){
            data_value_t* head = CAN_ID_array[(ID % 0x100)];
            if(0 == head->length){
                head = can_item;
            }
            else{
                while(head->next == NULL)head = head->next;
                head->next = can_item;
            }
        }
        index++;
    }

    can_data->indices_100Hz_p = indices_100Hz;
    can_data->length_100Hz_p = length_100Hz;

    can_data->indices_10Hz_p = indices_10Hz;
    can_data->length_10Hz_p = length_10Hz;

    can_data->indices_1Hz_p = indices_1Hz;
    can_data->length_1Hz_p = length_1Hz;

    cJSON_Delete(root);
    free(json_data);

    return ESP_OK;
}


esp_err_t finish_initialization(){
    free(init_structure);
    return ESP_OK;
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
