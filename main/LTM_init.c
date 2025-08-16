#include "LTM_init.h"

init_struct_t* init_structure;

static const char *TAG = "LTM init";

static uint32_t * indices_100Hz;
static uint32_t length_100Hz = 0;

static uint32_t * indices_10Hz;
static uint32_t length_10Hz = 0;

static uint32_t * indices_1Hz;
static uint32_t length_1Hz = 0;

esp_err_t SD_init(sdmmc_slot_config_t* SD_config){
    const char mountpoint[] = MOUNT_POINT;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    //host.max_freq_khz = SD_BUS_FREQ;
    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_config.max_files = 10;
    mount_config.format_if_mount_failed = false;
    mount_config.allocation_unit_size = 16 * 1024;

    ESP_ERROR_CHECK(esp_vfs_fat_sdmmc_mount(mountpoint, &host, SD_config, &mount_config, NULL));
    ESP_LOGI(TAG, "SD card has been initialized");
    return ESP_OK;
}

esp_err_t parse_JSON_globals_paddock(init_parameters params, cJSON* root);
esp_err_t parse_JSON_globals_car(init_parameters params, cJSON* root);

esp_err_t parse_JSON_globals(init_parameters params){
    char config_file_path[] = MOUNT_POINT"/config.txt";
    FILE* config_file = fopen(config_file_path,"r"); // might need to use "rb" also this is why .json files dont work :)
    if(NULL == config_file){
        ESP_LOGE(TAG,"couldn't open the file");
        return ESP_ERR_NO_MEM;
    }

    fseek(config_file, 0, SEEK_END);
    uint32_t length = ftell(config_file);
    fseek(config_file, 0, SEEK_SET);

    char* json_data = (char*)malloc((length+1) * sizeof(char));
    if(NULL == json_data){
        fclose(config_file);
        ESP_LOGE(TAG,"json_data failed to allocate");
        return ESP_ERR_NO_MEM;
    }

    fread(json_data, sizeof(char),length,config_file);
    json_data[length] = '\0';
    fclose(config_file);
    printf("%s\n",json_data);
    cJSON *root = cJSON_Parse(json_data);
    if(NULL == root){
        free(json_data);
        ESP_LOGE(TAG,"json root failed");
        return ESP_ERR_NOT_FINISHED;
    }

    LTM_type_t* LTM_type = params.LTM_type;

    cJSON* JSON_LTM_type = cJSON_GetObjectItemCaseSensitive(root,"TYPE");
    if (!cJSON_IsNumber(JSON_LTM_type)){
        cJSON_Delete(root);
        free(json_data);
        ESP_LOGE(TAG,"Invalid config");
        return ESP_ERR_INVALID_STATE;
    }
    *LTM_type = JSON_LTM_type->valueint;
    
    esp_err_t err;
    if(PADDOCK == *LTM_type){
        err = parse_JSON_globals_paddock(params, root);
    } else {
        err = parse_JSON_globals_car(params, root);
    }
    cJSON_Delete(root);
    free(json_data);

    return err;
}

esp_err_t parse_JSON_globals_paddock(init_parameters params, cJSON* root){
    paddock_array_t* paddock_array = params.paddock_array;
    uint64_t* channels = params.LoRa_channels;

    int channel_index = 0;

    cJSON* lap_timer_frequency = cJSON_GetObjectItemCaseSensitive(root,"LTF");
    if(lap_timer_frequency->valueint != 0){
        channels[channel_index] = lap_timer_frequency->valueint;
        channel_index++;
    }

    cJSON* JSON_paddock_array = cJSON_GetObjectItemCaseSensitive(root,"CARS");
    uint32_t array_len = cJSON_GetArraySize(JSON_paddock_array);
    paddock_state_t* paddock_state_array = malloc(sizeof(paddock_state_t) * array_len);
    if(NULL == paddock_state_array) return ESP_ERR_NO_MEM;

    cJSON* car_iterable;
    int index =0;
    cJSON_ArrayForEach(car_iterable,JSON_paddock_array){
        cJSON* cJSON_car_num = cJSON_GetObjectItemCaseSensitive(car_iterable,"CN");
        paddock_state_array[index].car_number = cJSON_car_num->valueint;
        cJSON* car_data_array = cJSON_GetObjectItemCaseSensitive(car_iterable,"ITEMS");
        paddock_state_array[index].array_length = cJSON_GetArraySize(car_data_array);

        cJSON* LoRa_frequency = cJSON_GetObjectItemCaseSensitive(car_iterable,"LF");
        if(LoRa_frequency->valueint != 0){
            channels[channel_index] = LoRa_frequency->valueint;
            channel_index++;
        }

        paddock_state_array[index].elements = malloc(sizeof(paddock_element_t) * paddock_state_array[index].array_length);
        if(NULL == paddock_state_array[index].elements){
            free(paddock_state_array);
            return ESP_ERR_NO_MEM;
        }

        cJSON* item_iterable = NULL;
        int jmdex = 0;
        cJSON_ArrayForEach(item_iterable,car_data_array){
            cJSON* conv = cJSON_GetObjectItemCaseSensitive(item_iterable,"C"); // lmao this shit is cooked :)
            cJSON* unit = cJSON_GetObjectItemCaseSensitive(item_iterable,"U");
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item_iterable,"N");
            cJSON* type = cJSON_GetObjectItemCaseSensitive(item_iterable,"T");
            cJSON* prec = cJSON_GetObjectItemCaseSensitive(item_iterable,"P");

            strcpy(paddock_state_array[index].elements[jmdex].name, name->valuestring);
            strcpy(paddock_state_array[index].elements[jmdex].unit, unit->valuestring);
            paddock_state_array[index].elements[jmdex].conversion = (float) conv->valuedouble / (float)prec->valuedouble;
            paddock_state_array[index].elements[jmdex].type = (char)type->valueint;
            paddock_state_array[index].elements[jmdex].data.u = 0;

            jmdex++;
        }
        index++;
    }

    paddock_array->cars = paddock_state_array;
    paddock_array->num_cars = array_len;

    return ESP_OK;
}

esp_err_t parse_JSON_globals_car(init_parameters params, cJSON* root){
    //// below is all of the car side parsing 
    valid_CAN_speeds_t* bus_speed = params.bus_speed;
    car_state_t* car_state = params.car_state;
    int* global_time_id = params.global_time_id;
    CAN_metadata_t* can_data = params.can_data;
    uint32_t** LoRa_array = params.LoRa_array;
    uint32_t* LoRa_array_length = params.LoRa_array_length;
    data_value_t*** CAN_ID_array = params.CAN_ID_array;
    uint64_t* LoRa_freq = params.LoRa_freq;

    cJSON* JSON_bus_speed = cJSON_GetObjectItemCaseSensitive(root,"BS");
    if (!cJSON_IsNumber(JSON_bus_speed)){
        ESP_LOGE(TAG,"Invalid config");
        return ESP_ERR_INVALID_STATE;
    }
        switch(JSON_bus_speed->valueint){
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

    cJSON* car_num = cJSON_GetObjectItemCaseSensitive(root, "CN");
    cJSON* GID = cJSON_GetObjectItemCaseSensitive(root, "GID");
    cJSON* freq = cJSON_GetObjectItemCaseSensitive(root, "LF");

    if(!cJSON_IsNumber(car_num) || !cJSON_IsNumber(GID) || !cJSON_IsNumber(freq)){
        ESP_LOGE(TAG,"Invalid config");
        return ESP_ERR_NOT_FINISHED;
    }
    car_state->car_number = car_num->valueint;
    *global_time_id = GID->valueint;
    *LoRa_freq = freq->valueint;
    ////////////////////////////////////////////////////// maybe exit here and use parse JSON can but for now its here feel free to seperate it justin
    cJSON* CAN_items = cJSON_GetObjectItemCaseSensitive(root,"CAN");

    uint32_t array_len = cJSON_GetArraySize(CAN_items);
    init_structure = malloc(sizeof(init_struct_t) * array_len);
    car_state->data_length = array_len;
    car_state->elements = malloc(sizeof(car_element_t)* array_len);

    indices_100Hz = malloc(sizeof(uint32_t) * array_len);
    indices_10Hz = malloc(sizeof(uint32_t) * array_len);
    indices_1Hz = malloc(sizeof(uint32_t) * array_len);

    *LoRa_array = malloc(sizeof(uint32_t) * array_len);
    if (*LoRa_array == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for LoRa_array");
        return ESP_ERR_NO_MEM;
    }

    *CAN_ID_array = calloc(MAX_CAN_ID_COUNT, sizeof(data_value_t*));

    if(NULL == car_state->elements){
        ESP_LOGE(TAG,"Invalid config");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON* interated_item = NULL;
    int index =0;
    cJSON_ArrayForEach(interated_item,CAN_items){
        cJSON* conv = cJSON_GetObjectItemCaseSensitive(interated_item,"C"); // lmao this shit is cooked :)
        cJSON* unit = cJSON_GetObjectItemCaseSensitive(interated_item,"U");
        cJSON* name = cJSON_GetObjectItemCaseSensitive(interated_item,"N");
        cJSON* type = cJSON_GetObjectItemCaseSensitive(interated_item,"T");
        cJSON* prec = cJSON_GetObjectItemCaseSensitive(interated_item,"P");
        cJSON* freq = cJSON_GetObjectItemCaseSensitive(interated_item,"F");
        cJSON* LoRa = cJSON_GetObjectItemCaseSensitive(interated_item,"L");
        cJSON* bitO = cJSON_GetObjectItemCaseSensitive(interated_item,"BO");
        cJSON* bitL = cJSON_GetObjectItemCaseSensitive(interated_item,"BL");
        cJSON* hxID = cJSON_GetObjectItemCaseSensitive(interated_item,"ID");
        strcpy(init_structure[index].name,name->valuestring);    // init_structure[index].name = name->valuestring;
        init_structure[index].conversion =(float) conv->valuedouble;
        strcpy(init_structure[index].unit, unit->valuestring);    //  init_structure[index].unit = unit->valuestring;
        init_structure[index].precision = (float) prec->valuedouble;
        car_state->elements[index].type = (char) type->valueint;
        car_state->elements[index].data.u = 0;
        //ESP_LOGI(TAG,"the frequency: %d",freq->valueint);
        //printf("%d\n",freq->valueint);
        switch(freq->valueint){
            case(100):
                indices_100Hz[length_100Hz] = index;
                length_100Hz++;
                break;
            case(10):
                    indices_10Hz[length_10Hz] = index;
                length_10Hz++;
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
        int ID = hxID->valueint;
        data_value_t * can_item = malloc(sizeof(data_value_t));
        can_item->index = index;
        can_item->offset = bit_offset;
        can_item->length = bit_length;
        can_item->next = NULL;
        //ESP_LOGW(TAG," new item: %p, its length: %d\n",can_item, can_item->length);
        if(ID / 0x100 == 7){
            //data_value_t* head = (*CAN_ID_array)[(ID % 0x100)];
            //printf("%p  |  \n", (void*)(*CAN_ID_array)[(ID % 0x100)]);
            if(NULL == (*CAN_ID_array)[(ID % 0x100)]){
                (*CAN_ID_array)[(ID % 0x100)] = can_item;
            }
            else{
                //ESP_LOGW(TAG,"%p  |  %p\n", (void*)(*CAN_ID_array)[(ID % 0x100)],(void*) ((*CAN_ID_array)[(ID % 0x100)])->next); ////////////////////////// delete these
                // vTaskDelay(pdMS_TO_TICKS(10)); ///////////////////////////////////////// delete this
                data_value_t* temp = ((*CAN_ID_array)[(ID % 0x100)]);
                while(temp->next != NULL) temp = temp->next;
                temp->next = can_item;
            }
        }
        index++;
        //printf("name: %s | bitOffset: %d | bitLength: %d | type: %c | CAN item index: %ld | ID: %d\n", name->valuestring, bitO->valueint, bitL->valueint, type->valueint, can_item->index, ID );
    }
    //ESP_LOGI(TAG,"%d",(*CAN_ID_array)[3]->length);
    can_data->indices_100Hz_p = indices_100Hz;
    can_data->length_100Hz_p = length_100Hz;

    can_data->indices_10Hz_p = indices_10Hz;
    can_data->length_10Hz_p = length_10Hz;

    can_data->indices_1Hz_p = indices_1Hz;
    can_data->length_1Hz_p = length_1Hz;

    ESP_LOGE(TAG, "100len: %ld, 10len: %ld, 1len: %ld", length_100Hz,length_10Hz,length_1Hz);

    //// the end of the car side parsing
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

    fclose(fptr_100HZ);
    fclose(fptr_10HZ);
    fclose(fptr_1HZ);
    
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
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%s", init_structure[indices[i]].name);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    write_data_no_free(buffer,++buffer_index,file);
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
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%s", init_structure[indices[i]].unit);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    write_data_no_free(buffer,++buffer_index,file);
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
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%.7f", init_structure[indices[i]].conversion);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    write_data_no_free(buffer,++buffer_index,file);
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
        bytes_written = snprintf(buffer + buffer_index, BUFFER_LENGTH - buffer_index, ",%ld", init_structure[indices[i]].precision);
        if(bytes_written > 0){
            buffer_index += bytes_written;
        }else{
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
    }
    buffer[buffer_index] = '\n';
    write_data(buffer,++buffer_index,file);
    return ESP_OK;
}
