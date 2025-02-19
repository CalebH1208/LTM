#include "LoRa.h"

static const char *TAG = "LoRa";


#define LoRa_OSCILLATOR_FREQUENCY 32000000.0f
#define LoRa_FREQ_ERROR_FACTOR ((1 << 24) / LoRa_OSCILLATOR_FREQUENCY)
#define FIFO_TX_BASE_ADDR 0b00000000
#define FIFO_RX_BASE_ADDR 0b00000000
#define LoRa_REG_MODEM_CONFIG_3_AGC_ON 0b00000100
#define LoRa_REG_MODEM_CONFIG_3_AGC_OFF 0b00000000
#define LoRa_IRQ_FLAG_RXTIMEOUT 0b10000000
#define LoRa_IRQ_FLAG_RXDONE 0b01000000
#define LoRa_IRQ_FLAG_PAYLOAD_CRC_ERROR 0b00100000
#define LoRa_IRQ_FLAG_VALID_HEADER 0b00010000
#define LoRa_IRQ_FLAG_TXDONE 0b00001000
#define LoRa_IRQ_FLAG_CADDONE 0b00000100
#define LoRa_IRQ_FLAG_FHSSCHANGECHANNEL 0b00000010
#define LoRa_IRQ_FLAG_CAD_DETECTED 0b00000001
#define LoRa_VERSION 0x12
#define RF_MID_BAND_THRESHOLD 525000000
#define RSSI_OFFSET_HF_PORT 157
#define RSSI_OFFSET_LF_PORT 164
#define LoRa_HIGH_POWER_ON 0b10000111
#define LoRa_HIGH_POWER_OFF 0b10000100
#define LoRa_MAX_POWER 0b01110000
#define LoRa_LOW_POWER 0b00000000

#define ERROR_CHECK(x)           \
  do {                           \
    int __err_rc = (x);          \
    if (__err_rc != ESP_OK) { \
      return __err_rc;           \
    }                            \
  } while (0)

#define CHECK_MODULATION(x, y)         \
  do {                                 \
    if (x->active_modem != y) {        \
      return ESP_ERR_INVALID_STATE; \
    }                                  \
  } while (0)

#define ERROR_CHECK_NOCODE(x)    \
  do {                           \
    int __err_rc = (x);          \
    if (__err_rc != ESP_OK) { \
      return;                    \
    }                            \
  } while (0)

int total_packets_received = 0;
const UBaseType_t xArrayIndex = 0;
TaskHandle_t handle_interrupt;
void (*global_tx_callback)(LoRaModule *device);


LoRaModule* LoRa_initialize(int mosi,int miso,int clk,int nss,uint32_t clockSpeed,spi_host_device_t host, spi_device_handle_t* handle,void** DMA){
    spi_bus_config_t spi ={
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };

    spi_device_interface_config_t device = {0};
    device.address_bits = 8;
    device.clock_source = SPI_CLK_SRC_XTAL;
    device.mode = 0;
    device.clock_speed_hz = clockSpeed; // probably 10000000; 
    device.spics_io_num = nss;
    device.queue_size = 16;

    
    LoRaModule* ret = malloc(sizeof(LoRaModule));
    memset(ret, 0, sizeof(ret));
    esp_err_t check = spi_initialize_device(&spi,&device,host, handle, DMA);
    if(check != ESP_OK)return NULL;
    ret->device = handle;
    ret->mode = LoRa_MODE_SLEEP;
    uint8_t version;
    int code = Spi_read_register(ret->device,REGVERSION,1,(uint32_t*)&version);
    if (code != ESP_OK) {
        return NULL;
    }
    if (version != LoRa_VERSION) {
        return NULL;
    }
    ret->active_modem = CHIP_MODULATION_LORA;
    ret->mode = LoRa_MODE_STANDBY;
    ret->use_implicit_header = false;
    ret->expected_packet_length = 0;
    ESP_LOGI(TAG,"LoRa should be initialized");
    return ret;
}  

void LoRa_reset(int rst){
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)rst,GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)rst,0));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)rst,1));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG,"LoRa Module reset");
}

int LoRa_set_low_datarate_optimization(bool enable,LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    uint8_t value = enable? 0b00001000:0b0000000;
    return Spi_append_register(chip->device,REGMODEMCONFIG3,0b11110111,value);
}

int LoRa_get_bandwidth(LoRaModule* chip, uint32_t * bandwidth){
    CHECK_MODULATION(chip, CHIP_MODULATION_LORA);
    uint8_t config = 0;
    ERROR_CHECK(Spi_read_register(chip->device,REGMODEMCONFIG1,1,(uint32_t*)&config));
    config = (config >> 4);
    switch (config) {
    case 0b0000:
      *bandwidth = 7800;
      break;
    case 0b0001:
      *bandwidth = 10400;
      break;
    case 0b0010:
      *bandwidth = 15600;
      break;
    case 0b0011:
      *bandwidth = 20800;
      break;
    case 0b0100:
      *bandwidth = 31250;
      break;
    case 0b0101:
      *bandwidth = 41700;
      break;
    case 0b0110:
      *bandwidth = 62500;
      break;
    case 0b0111:
      *bandwidth = 125000;
      break;
    case 0b1000:
      *bandwidth = 250000;
      break;
    case 0b1001:
      *bandwidth = 500000;
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

int LoRa_reload_low_datarate_optimization(LoRaModule* chip){
    uint32_t bandwidth;
    ERROR_CHECK(LoRa_get_bandwidth(chip, &bandwidth));
    uint8_t spreading_factor = 0;
    ERROR_CHECK(Spi_read_register(chip->device,REGMODEMCONFIG2, 1, (uint32_t*)&spreading_factor));
    spreading_factor = (spreading_factor >> 4);

    uint32_t symbol_duration = 1000 / (bandwidth / (1L << spreading_factor));
    if (symbol_duration > 16) {
        // force low data rate optimization
        return LoRa_set_low_datarate_optimization(true, chip);
    } else {
        return LoRa_set_low_datarate_optimization(false, chip);
    }
}

int LoRa_rx_read_payload(LoRaModule* chip){ // potentially a bug in here maybe? something with getting the length of the packet
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    uint8_t length;
    if(chip->expected_packet_length == 0){
        ERROR_CHECK(Spi_read_register(chip->device,REGRXNBBYTES,1,(uint32_t*)&length));
    } else{
        length = (uint8_t) chip->expected_packet_length;
    }
    chip->expected_packet_length = length;
    uint8_t current;
    ERROR_CHECK(Spi_read_register(chip->device,REGFIFORXCURRENTADDR, 1, (uint32_t*)&current));
    ERROR_CHECK(Spi_write_register(chip->device,REGFIFOADDRPTR,&current,1));
    return (Spi_read_buffer(chip->device,REGFIFO,chip->expected_packet_length,chip->packet));
}

void LoRa_handle_interrupt(LoRaModule* chip){
    uint8_t value;
    //ESP_LOGE(TAG,"right before: %p",(void*)(((LoRaModule*)chip)->device));
    ERROR_CHECK_NOCODE(Spi_read_register(chip->device,REGIRQFLAGS,1,(uint32_t*)&value));
    ERROR_CHECK_NOCODE(Spi_write_register(chip->device,REGIRQFLAGS,&value,1));
    if ((value & LoRa_IRQ_FLAG_CADDONE) != 0) {
    if (chip->cad_callback != NULL) {
      chip->cad_callback(chip, value & LoRa_IRQ_FLAG_CAD_DETECTED);
    }
        return;
    }
    if ((value & LoRa_IRQ_FLAG_PAYLOAD_CRC_ERROR) != 0) {
        chip->current_frequency = 0;
        return;
    }
    if ((value & LoRa_IRQ_FLAG_RXDONE) != 0) {
        ERROR_CHECK_NOCODE(LoRa_rx_read_payload(chip));
        if (chip->rx_callback != NULL) {
        chip->rx_callback(chip, chip->packet, chip->expected_packet_length);
        }
        chip->expected_packet_length = 0;
        chip->current_frequency = 0;
        return;
    }
    if ((value & LoRa_IRQ_FLAG_TXDONE) != 0) {
        chip->current_frequency = 0;
        if (chip->tx_callback != NULL) {
        chip->tx_callback(chip);
        }
        return;
    }
    if ((value & LoRa_IRQ_FLAG_FHSSCHANGECHANNEL) != 0) {
        if (chip->current_frequency >= chip->frequencies_length) {
        chip->current_frequency = 0;
        }
        ERROR_CHECK_NOCODE(LoRa_set_frequency(chip->frequencies[chip->current_frequency], chip));
        chip->current_frequency++;
        return;
    }
}

int LoRa_set_mode(LoRa_mode_t mode,LoRaModule* chip){
    if(mode == LoRa_MODE_RX_SINGLE || LoRa_MODE_RX_CONT){
        uint8_t data = (LoRa_DIO0_RX_DONE | LoRa_DIO1_RXTIMEOUT | LoRa_DIO2_FHSS_CHANGE_CHANNEL | LoRa_DIO3_CAD_DONE);
        ERROR_CHECK(Spi_write_register(chip->device,REGDIOMAPPING1,&data,1));
    }
    else if(mode == LoRa_MODE_TX){
        uint8_t data = (LoRa_DIO0_TX_DONE | LoRa_DIO1_FHSS_CHANGE_CHANNEL | LoRa_DIO2_FHSS_CHANGE_CHANNEL | LoRa_DIO3_CAD_DONE);
        ERROR_CHECK(Spi_write_register(chip->device,REGDIOMAPPING1,&data,1));
    }   
    uint8_t value = (mode | CHIP_MODULATION_LORA);
    int result = Spi_write_register(chip->device,REGOPMODE,&value,1);
    if (result == ESP_OK) {
        chip->active_modem = CHIP_MODULATION_LORA;
        chip->mode = mode;
    }
    return result;
}

int LoRa_set_frequency(uint64_t frequency,LoRaModule* chip){
    uint64_t adjusted = (frequency << 19) / 32000000.0f;
    uint8_t data[] = {(uint8_t)(adjusted>>16),(uint8_t)(adjusted>>8),(uint8_t)(adjusted>>0)};
    ERROR_CHECK(Spi_write_register(chip->device,REGFRFMSB,data,3));
    return ESP_OK;
}

int LoRa_get_frequency( LoRaModule* chip, uint64_t* freq){
    uint32_t raw;
    ERROR_CHECK(Spi_read_register(chip->device,REGFRFMSB,3,&raw));
    *freq = (uint64_t) (raw *32000000.0f)>>19;
    return ESP_OK;
}

int LoRa_reset_fifo(LoRaModule* chip){
    CHECK_MODULATION(chip, CHIP_MODULATION_LORA);
    uint8_t data[] = {FIFO_TX_BASE_ADDR, FIFO_RX_BASE_ADDR};
    return Spi_write_register(chip->device,REGFIFOTXBASEADDR,data,2);
}

int LoRa_rx_set_lna_gain(LoRaModule* chip,LoRa_gain_t gain){
if (chip->active_modem == CHIP_MODULATION_LORA) {
    if (gain == LoRa_LNA_GAIN_AUTO) {
      return Spi_append_register(chip->device,REGMODEMCONFIG3, 0b11111011, LoRa_REG_MODEM_CONFIG_3_AGC_ON);
    }
    ERROR_CHECK(Spi_append_register(chip->device,REGMODEMCONFIG3, 0b11111011, LoRa_REG_MODEM_CONFIG_3_AGC_OFF));
    return Spi_append_register(chip->device,REGLNA,0b00011111, gain);
  } else if (chip->active_modem == CHIP_MODULATION_FSK || chip->active_modem == CHIP_MODULATION_OOK) {
    // gain manual
    return ESP_ERR_NOT_SUPPORTED;
  } else {
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_ERR_INVALID_ARG;
}

int LoRa_rx_set_lna_boost_hf(LoRaModule* chip,bool enable){
    uint8_t value = (enable ? 0b00000011 : 0b00000000);
    return Spi_append_register(chip->device,REGLNA,0b11111100, value);
}

int LoRa_set_bandwidth(LoRaModule* chip,LoRa_bw_t bw){
    CHECK_MODULATION(chip, CHIP_MODULATION_LORA);
    ERROR_CHECK(Spi_append_register(chip->device,REGMODEMCONFIG1,0b00001111,bw));
    return LoRa_reload_low_datarate_optimization(chip);
}

int LoRa_set_modem_config_2(LoRa_sf_t spreading_factor,LoRaModule *chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    if(spreading_factor == LoRa_SF_6 && chip->use_implicit_header){
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t detection_optimize;
    uint8_t detection_threshold;
    if (spreading_factor == LoRa_SF_6) {
        detection_optimize = 0xc5;
        detection_threshold = 0x0c;
        // make header implicit
    } else {
        detection_optimize = 0xc3;
        detection_threshold = 0x0a;
    }
    ERROR_CHECK(Spi_write_register(chip->device,REGDETECTOPTIMIZE,&detection_optimize,1));
    ERROR_CHECK(Spi_write_register(chip->device,REGDETECTIONTHRESHOLD,&detection_threshold,1));
    ERROR_CHECK(Spi_append_register(chip->device,REGMODEMCONFIG2,0b00001111,spreading_factor));
    return LoRa_reload_low_datarate_optimization(chip);
}

void LoRa_rx_set_callback(void (*rx_callback)(LoRaModule*,uint8_t*,uint16_t),LoRaModule* chip){
    chip->rx_callback = rx_callback;
}

int LoRa_set_syncword(uint8_t value, LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    return Spi_write_register(chip->device,REGSYNCWORD,&value,1);
}

int LoRa_set_preamble_length(uint16_t value,LoRaModule* chip){
    uint8_t data[] = {(uint8_t)(value>>8),(uint8_t)(value>>0)};
    if(chip->active_modem == CHIP_MODULATION_LORA){
        return Spi_write_register(chip->device,REGPREAMBLEMSB,data,2);
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }
}

int LoRa_set_implicit_header(LoRa_implicit_header_t * header,LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    if(header == NULL){
        chip->expected_packet_length = 0;
        chip->use_implicit_header = false;
        return Spi_append_register(chip->device,REGMODEMCONFIG1,0b11111110,LoRa_HEADER_MODE_EXPLICIT);
    }
    else{
        chip->expected_packet_length = header->length;
        chip->use_implicit_header = true;
        ERROR_CHECK(Spi_append_register(chip->device,REGMODEMCONFIG1 ,0b11110000,LoRa_HEADER_MODE_IMPLICIT| header->coding_rate));
        ERROR_CHECK(Spi_write_register(chip->device,REGPAYLOADLENGTH,&(header->length),1));
        uint8_t value = (header->enable_crc ? 0b00000100 : 0b00000000);
        return Spi_append_register(chip->device,REGMODEMCONFIG2,0b11111011,value);
    }
}

int LoRa_set_frequency_hopping(uint8_t period,uint64_t *frequencies,uint8_t frequencies_length,LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    if(frequencies == NULL || frequencies_length == 0){
        return ESP_ERR_INVALID_ARG;
    }
    chip->frequencies = frequencies;
    chip->frequencies_length = frequencies_length;
    return Spi_write_register(chip->device,REGHOPPERIOD,&period,1);
}

int LoRa_rx_get_packet_rssi(LoRaModule* chip, int16_t* rssi){ // keep and eye on this one
    if(chip->active_modem == CHIP_MODULATION_LORA){
        uint8_t value;
        ERROR_CHECK(Spi_read_register(chip->device,REGPKTRSSIVALUE,1,(uint32_t*)&value));
        uint64_t frequency;
        ERROR_CHECK(LoRa_get_frequency(chip,&frequency));
        if(frequency < RF_MID_BAND_THRESHOLD){
            *rssi = value - RSSI_OFFSET_LF_PORT;
        }
        else{
            *rssi = value - RSSI_OFFSET_HF_PORT;
        }
        float snr;
        int code = LoRa_rx_get_packet_snr(chip,&snr);
        if(code == ESP_OK && snr < 0){
            *rssi = *rssi + snr;
        }
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

int LoRa_rx_get_packet_snr(LoRaModule* chip,float *snr){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    uint8_t value;
    ERROR_CHECK(Spi_read_register(chip->device,REGPKTSNRVALUE,1,(uint32_t*)&value));
    *snr = (float) ((int8_t)value)*0.25f;
    return ESP_OK;
}

int LoRa_rx_get_frequency_error(LoRaModule* chip, int32_t *result){
    if(chip->active_modem == CHIP_MODULATION_LORA){
        uint32_t frequency_error;
        ERROR_CHECK(Spi_read_register(chip->device,REGFEIMSB,3,&frequency_error));
        uint32_t bandwidth;
        ERROR_CHECK(Spi_read_register(chip->device,REGFEIMSB,3,&bandwidth));
        if(frequency_error & 0x80000){
            frequency_error = ((~frequency_error)+1) &0xFFFFF;
            *result = -1;
        }
        else{
            *result = -1;
        }
        *result = (*result) * (frequency_error * LoRa_FREQ_ERROR_FACTOR * bandwidth / 500000.0f);
        return ESP_OK;
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_INVALID_ARG;
}

int LoRa_dump_registers(uint8_t* output,LoRaModule* chip){
    output[0] = 0x00;
    return Spi_read_buffer(chip->device,0x01,MAX_NUMBER_OF_REGISTERS -1,output+1);
}

void LoRa_tx_set_callback(void (*tx_callback)(LoRaModule*),LoRaModule* chip){
    chip->tx_callback = tx_callback;
}

int LoRa_tx_set_pa_config(LoRa_pa_pin_t pin,int power,LoRaModule* chip){
    if(pin == LoRa_PA_PIN_RFO && (power<-4 || power>15)){
        return ESP_ERR_INVALID_ARG;
    }
    if(pin == LoRa_PA_PIN_BOOST && (power<2 || power>20 || power ==18||power==19)){ // lmao what the fuck
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[] = {0};
    if(pin == LoRa_PA_PIN_BOOST && power ==20){
        data[0] = LoRa_HIGH_POWER_ON;
    }
    else{
        data[0] = LoRa_HIGH_POWER_OFF;
    }
    ERROR_CHECK(Spi_write_register(chip->device,REGPADAC,data,1));
    uint8_t max_current;
    if(pin == LoRa_PA_PIN_BOOST){
        if(power == 20){
            max_current = 120;
        }
        else{
            max_current = 87;
        }
    }
    else{
        if(power > 7){
            max_current = 29;
        }
        else{
            max_current = 20;
        }
    }
    ERROR_CHECK(LoRa_tx_set_ocp(true,max_current,chip));
    uint8_t value;
    if(pin == LoRa_PA_PIN_RFO){
        if (power < 0){
            value = LoRa_LOW_POWER | (power+4);
        }
        else{
            value = LoRa_MAX_POWER | power;
        }
        value = value | LoRa_PA_PIN_RFO;
    }
    else{
        if(power == 20){
            value = LoRa_PA_PIN_BOOST | 0b00001111;
        }
        else{
            value = LoRa_PA_PIN_BOOST | (power - 2);
        }
    }
    return Spi_write_register(chip->device,REGPACONFIG,&value,1);
}

int LoRa_tx_set_ocp(bool enable, uint8_t max_current, LoRaModule* chip){
    if(max_current < 45){
        return ESP_ERR_INVALID_ARG;
    }
    if(!enable){
        uint8_t value = 0b00000000;
        return Spi_write_register(chip->device,REGOCP,&value,1);
    }
    uint8_t value;
    if (max_current <= 120){
        value = (max_current - 45)/5;
    }
    else if (max_current <= 240){
        value = (max_current + 30) / 10;
    }
    else{
        value = 27;
    }
    value |= 0b00100000;
    return Spi_write_register(chip->device,REGOCP,&value,1);
}

int LoRa_tx_set_explicit_header(LoRa_tx_header_t *header, LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    if(header == NULL) return ESP_ERR_INVALID_ARG;
    chip->use_implicit_header = false;
    chip->expected_packet_length = 0;
    ERROR_CHECK(Spi_append_register(chip->device,REGMODEMCONFIG1, 0b11110000, header->coding_rate | LoRa_HEADER_MODE_EXPLICIT));
    uint8_t value = (header->enable_crc ? 0b00000100 : 0b0000000);
    return Spi_append_register(chip->device,REGMODEMCONFIG1, 0b11111011,value);
}

int LoRa_tx_set_for_transmission(const uint8_t* data,uint8_t data_length, LoRaModule* chip){
    CHECK_MODULATION(chip,CHIP_MODULATION_LORA);
    if(data_length == 0){
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t fifo_addr[] = {FIFO_TX_BASE_ADDR};
    ERROR_CHECK(Spi_write_register(chip->device,REGFIFOADDRPTR,fifo_addr,1));
    uint8_t reg_data[] = {data_length}; 
    ERROR_CHECK(Spi_write_register(chip->device,REGPAYLOADLENGTH,reg_data,1));
    return Spi_write_buffer(chip->device,REGFIFO, (uint8_t*)data, data_length);
}

int LoRa_set_ppm_offset(int32_t frequency_error, LoRaModule* chip){
    uint64_t frequency;
    ERROR_CHECK(LoRa_get_frequency(chip,&frequency));
    uint8_t value = (uint8_t) (0.95f * ((float) frequency_error / (frequency / 1E6f)));
    return Spi_write_register(chip->device, 0x27,&value,1);
}

void LoRa_cad_set_callback(void (*cad_callback)(LoRaModule*, int),LoRaModule* chip){
    chip->cad_callback = cad_callback;
}

void IRAM_ATTR handle_interrupt_fromisr(void* arg){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveIndexedFromISR(handle_interrupt, xArrayIndex,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// replace the code below this with your own shit liberal cuck

void handle_interrupt_rx_task(void *arg){
    while(1){
        if (ulTaskNotifyTakeIndexed(xArrayIndex, pdTRUE, portMAX_DELAY) > 0){
            //ESP_LOGE(TAG,"what the value should be: %p",(void*)(((LoRaModule*)arg)->device));
            //ESP_LOGI(TAG,"start signal reception");
            LoRa_handle_interrupt((LoRaModule*)arg);
            //ESP_LOGI(TAG,"end signal reception");
        }
    }
}

void handle_interrupt_tx_task(void* arg){
    global_tx_callback((LoRaModule*)arg);
    while(1){
        if(ulTaskNotifyTakeIndexed(xArrayIndex, pdTRUE, portMAX_DELAY) > 0){
            LoRa_handle_interrupt((LoRaModule*) arg);
        }
    }
}

uint32_t messages_recieved = 0;

void LoRa_rx_callback(LoRaModule* chip, uint8_t* data, uint16_t length){
    messages_recieved++;
    // uint8_t payload[257];
    // //const char SYMBOLS[] = "0123456789ABCDEF";
    // for (size_t i = 0; i < length; i++) {
    //     //uint8_t cur = data[i];
    //     //payload[2 * i] = SYMBOLS[cur >> 4];
    //     //payload[2 * i + 1] = SYMBOLS[cur & 0x0F];
    //     payload[i] = data[i];
    // }
    // payload[length] = '\0';
    // int16_t rssi;
    // ESP_ERROR_CHECK(LoRa_rx_get_packet_rssi(chip,&rssi));
    // float snr;
    // ESP_ERROR_CHECK(LoRa_rx_get_packet_snr(chip, &snr));
    // int32_t frequency_error;
    // ESP_ERROR_CHECK(LoRa_rx_get_frequency_error(chip,&frequency_error));
    // ESP_LOGI(TAG, "received: %d %s rssi: %d snr: %f freq_error: %" PRId32, length, payload, rssi, snr, frequency_error);
    // total_packets_received++;
}

void updateshit(int seconds){
    ESP_LOGI(TAG,"total msgs: %lu, per second: %.2f",messages_recieved,messages_recieved / (float)seconds);
    messages_recieved = 0;
}

void cad_callback(LoRaModule* chip,int cad_detected){
    if (cad_detected == 0) {
        ESP_LOGI(TAG, "cad not detected");
        ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_CAD, chip));
        return;
    }
    // put into RX mode first to handle interrupt as soon as possible
    ESP_ERROR_CHECK(LoRa_set_mode(LoRa_MODE_RX_CONT, chip));
    ESP_LOGI(TAG, "cad detected\n");
}

void setup_gpio_interrupts(gpio_num_t gpio, LoRaModule *chip, gpio_int_type_t type){
  ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_INPUT));
  ESP_ERROR_CHECK(gpio_pulldown_en(gpio));
  ESP_ERROR_CHECK(gpio_pullup_dis(gpio));
  ESP_ERROR_CHECK(gpio_set_intr_type(gpio, type));
  ESP_ERROR_CHECK(gpio_isr_handler_add(gpio, handle_interrupt_fromisr, (void *) chip));
}

esp_err_t setup_rx_task(LoRaModule *chip){
    //ESP_LOGE(TAG,"what the value should be on init: %p",(void*)chip->device);
  BaseType_t task_code = xTaskCreate(handle_interrupt_rx_task, "handle interrupt", 16384, chip, 2, &handle_interrupt);
  if (task_code != pdPASS) {
    ESP_LOGE(TAG, "can't create task %d", task_code);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t setup_tx_task(LoRaModule *chip, void (*tx_callback)(LoRaModule *chip)){
  global_tx_callback = tx_callback;
  BaseType_t task_code = xTaskCreatePinnedToCore(handle_interrupt_tx_task, "handle tx interrupt", 8196, chip, 2, &handle_interrupt, xPortGetCoreID());
  if (task_code != pdPASS) {
    ESP_LOGE(TAG, "can't create task %d", task_code);
    return ESP_FAIL;
  }
  return ESP_OK;
}