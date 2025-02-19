#ifndef LORA_H
#define LORA_H
#include <inttypes.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "Spi.h"

#define REGFIFO 0x00
#define REGOPMODE 0x01
#define REGFRFMSB 0x06
#define REGFRFMID 0x07
#define REGFRFLSB 0x08
#define REGPACONFIG 0x09
#define REGPARAMP 0x0a
#define REGOCP 0x0b
#define REGLNA 0x0c
#define REGFIFOADDRPTR 0x0d
#define REGFIFOTXBASEADDR 0x0e
#define REGFIFORXBASEADDR 0x0f
#define REGFIFORXCURRENTADDR 0x10
#define REGIRQFLAGSMASK 0x11
#define REGIRQFLAGS 0x12
#define REGRXNBBYTES 0x13
#define REGRXHEADERCNTVALUEMSB 0x14
#define REGRXHEADERCNTVALUELSB 0x15
#define REGRXPACKETCNTVALUEMSB 0x16
#define REGRXPACKETCNTVALUELSB 0x17
#define REGMODEMSTAT 0x18
#define REGPKTSNRVALUE 0x19
#define REGPKTRSSIVALUE 0x1a
#define REGRSSIVALUE 0x1b
#define REGHOPCHANNEL 0x1c
#define REGMODEMCONFIG1 0x1d
#define REGMODEMCONFIG2 0x1e
#define REGSYMBTIMEOUTLSB 0x1f
#define REGPREAMBLEMSB 0x20
#define REGPREAMBLELSB 0x21
#define REGPAYLOADLENGTH 0x22
#define REGMAXPAYLOADLENGTH 0x23
#define REGHOPPERIOD 0x24
#define REGFIFORXBYTEADDR 0x25
#define REGMODEMCONFIG3 0x26
#define REGFEIMSB 0x28
#define REGFEIMID 0x29
#define REGFEILSB 0x2a
#define REGRSSIWIDEBAND 0x2c
#define REGDETECTOPTIMIZE 0x31
#define REGINVERTIQ 0x33
#define REGDETECTIONTHRESHOLD 0x37
#define REGSYNCWORD 0x39
#define REGTIMER2COEF 0x3a
#define REGIMAGECAL 0x3b
#define REGTEMP 0x3c
#define REGLOWBAT 0x3d
#define REGIRQFLAGS1 0x3e
#define REGIRQFLAGS2 0x3f
#define REGDIOMAPPING1 0x40
#define REGDIOMAPPING2 0x41
#define REGVERSION 0x42
#define REGPLLHOP 0x44
#define REGTCXO 0x4b
#define REGPADAC 0x4d
#define REGFORMERTEMP 0x5b
#define REGBITRATEFRAC 0x5d
#define REGAGCREF 0x61
#define REGAGCTHRESH1 0x62
#define REGAGCTHRESH2 0x63
#define REGAGCTHRESH3 0x64

// Band-specific registers
#define REGAGCREFLF 0x61
#define REGAGCTHRESH1LF 0x62
#define REGAGCTHRESH2LF 0x63
#define REGAGCTHRESH3LF 0x64
#define REGPLLLF 0x70

#define REGAGCREFHF 0x61
#define REGAGCTHRESH1HF 0x62
#define REGAGCTHRESH2HF 0x63
#define REGAGCTHRESH3HF 0x64
#define REGPLLHF 0x70

#define MAX_PACKET_SIZE 255
#define MAX_PACKET_SIZE_FSK_FIXED 2047
#define MAX_NUMBER_OF_REGISTERS 0x71


typedef enum {
  LoRa_HEADER_MODE_EXPLICIT = 0b00000000,
  LoRa_HEADER_MODE_IMPLICIT = 0b00000001
} LoRa_header_mode_t;


typedef enum {
  LoRa_CR_4_5 = 0b00000010, // default
  LoRa_CR_4_6 = 0b00000100,
  LoRa_CR_4_7 = 0b00000110,
  LoRa_CR_4_8 = 0b00001000
} LoRa_cr_t;

typedef enum {
  CHIP_MODULATION_LORA = 0b10000000,
  CHIP_MODULATION_FSK = 0b00000000, // default
  CHIP_MODULATION_OOK = 0b00100000
} Chip_modulation_t;


typedef enum {
  LoRa_BW_7800 = 0b00000000,
  LoRa_BW_10400 = 0b00010000,
  LoRa_BW_15600 = 0b00100000,
  LoRa_BW_20800 = 0b00110000,
  LoRa_BW_31250 = 0b01000000,
  LoRa_BW_41700 = 0b01010000,
  LoRa_BW_62500 = 0b01100000,
  LoRa_BW_125000 = 0b01110000, // default
  LoRa_BW_250000 = 0b10000000,
  LoRa_BW_500000 = 0b10010000
} LoRa_bw_t;

typedef enum {
    LoRa_MODE_SLEEP = 0b00000000,      // SLEEP
    LoRa_MODE_STANDBY = 0b00000001,    // STDBY
    LoRa_MODE_FSTX = 0b00000010,       // Frequency synthesis TX
    LoRa_MODE_TX = 0b00000011,         // Transmit
    LoRa_MODE_FSRX = 0b00000100,       // Frequency synthesis RX
    LoRa_MODE_RX_CONT = 0b00000101,    // Receive continuous
    LoRa_MODE_RX_SINGLE = 0b00000110,  // Receive single
    LoRa_MODE_CAD = 0b00000111         // Channel activity detection
} LoRa_mode_t;



typedef enum {
  LoRa_DIO0_RX_DONE = 0b00000000,              // Packet reception complete
  LoRa_DIO0_TX_DONE = 0b01000000,              // FIFO Payload transmission complete
  LoRa_DIO0_CAD_DONE = 0b10000000,             // CAD complete
  LoRa_DIO1_RXTIMEOUT = 0b00000000,            // RX timeout interrupt. Used in RX single mode
  LoRa_DIO1_FHSS_CHANGE_CHANNEL = 0b00010000,  // FHSS change channel
  LoRa_DIO1_CAD_DETECTED = 0b00100000,         // Valid Lora signal detected during CAD operation
  LoRa_DIO2_FHSS_CHANGE_CHANNEL = 0b00000000,  // FHSS change channel on digital pin 2
  LoRa_DIO3_CAD_DONE = 0b00000000,             // CAD complete on digital pin 3
  LoRa_DIO3_VALID_HEADER = 0b00000001,         // Valid header received in Rx
  LoRa_DIO3_PAYLOAD_CRC_ERROR = 0b00000010,    // Payload CRC error
} LoRa_dio_mapping1_t;

typedef enum {
  LoRa_DIO4_CAD_DETECTED = 0b00000000,  // Valid Lora signal detected during CAD operation
  LoRa_DIO4_PLL_LOCK = 0b01000000,      // PLL lock
  LoRa_DIO5_MODE_READY = 0b00000000,    // Mode ready
  LoRa_DIO5_CLK_OUT = 0b01000000        // clock out
} LoRa_dio_mapping2_t;

typedef enum {
  LoRa_PA_PIN_RFO = 0b00000000,   // RFO pin. Output power is limited to +14 dBm.
  LoRa_PA_PIN_BOOST = 0b10000000  // PA_BOOST pin. Output power is limited to +20 dBm
} LoRa_pa_pin_t;

typedef enum {
  LoRa_LNA_GAIN_G1 = 0b00100000,  // Maximum gain
  LoRa_LNA_GAIN_G2 = 0b01000000,
  LoRa_LNA_GAIN_G3 = 0b01100000,
  LoRa_LNA_GAIN_G4 = 0b10000000,
  LoRa_LNA_GAIN_G5 = 0b10100000,
  LoRa_LNA_GAIN_G6 = 0b11000000,   // Minimum gain
  LoRa_LNA_GAIN_AUTO = 0b00000000  // Automatic. See 5.5.3. for details
} LoRa_gain_t;

typedef enum {
  LoRa_SF_6 = 0b01100000,   // 64 chips / symbol
  LoRa_SF_7 = 0b01110000,   // 128 chips / symbol
  LoRa_SF_8 = 0b10000000,   // 256 chips / symbol
  LoRa_SF_9 = 0b10010000,   // 512 chips / symbol
  LoRa_SF_10 = 0b10100000,  // 1024 chips / symbol
  LoRa_SF_11 = 0b10110000,  // 2048 chips / symbol
  LoRa_SF_12 = 0b11000000   // 4096 chips / symbol
} LoRa_sf_t;

typedef struct LoRaModule LoRaModule;

typedef struct LoRaModule {
  spi_device_handle_t* device;
  bool use_implicit_header;
  void (*rx_callback)(LoRaModule *, uint8_t *, uint16_t);
  void (*tx_callback)(LoRaModule *);
  void (*cad_callback)(LoRaModule *, int);
  uint8_t packet[MAX_PACKET_SIZE_FSK_FIXED];
  uint16_t expected_packet_length;
  Chip_modulation_t active_modem;
  LoRa_mode_t mode;
  uint64_t *frequencies;
  uint8_t frequencies_length;
  uint8_t current_frequency;
  gpio_num_t Activity_LED;
}LoRaModule;

typedef struct {
  uint8_t length;           // payload length. Cannot be more than 256 bytes.
  bool enable_crc;          // Enable or disable CRC.
  LoRa_cr_t coding_rate;  // Coding rate
} LoRa_implicit_header_t;

typedef struct {
  bool enable_crc;
  LoRa_cr_t coding_rate;
} LoRa_tx_header_t;



LoRaModule* LoRa_initialize(int mosi,int miso,int clk,int nss,uint32_t clockSpeed,spi_host_device_t host,spi_device_handle_t* handle,void** DMA);

void LoRa_reset(int rst);

int LoRa_set_low_datarate_optimization(bool enable,LoRaModule* chip);

int LoRa_get_bandwidth(LoRaModule* chip, uint32_t * bandwidth);

int LoRa_reload_low_datarate_optimization(LoRaModule* chip);

int LoRa_rx_read_payload(LoRaModule* chip);

void LoRa_handle_interrupt(LoRaModule* chip);

int LoRa_set_mode(LoRa_mode_t mode,LoRaModule* chip);

int LoRa_set_frequency(uint64_t frequency,LoRaModule* chip);

int LoRa_get_frequency(LoRaModule* chip, uint64_t* freq);

int LoRa_reset_fifo(LoRaModule* chip);

int LoRa_rx_set_lna_gain(LoRaModule* chip,LoRa_gain_t gain);

int LoRa_rx_set_lna_boost_hf(LoRaModule* chip,bool enable);

int LoRa_set_bandwidth(LoRaModule* chip,LoRa_bw_t bw);

int LoRa_set_modem_config_2(LoRa_sf_t spreading_factor,LoRaModule *chip);

void LoRa_rx_set_callback(void (*rx_callback)(LoRaModule*,uint8_t*,uint16_t),LoRaModule* chip);

int LoRa_set_syncword(uint8_t value, LoRaModule* chip);

int LoRa_set_preamble_length(uint16_t value,LoRaModule* chip);

int LoRa_set_implicit_header(LoRa_implicit_header_t * header,LoRaModule* chip);

int LoRa_set_frequency_hopping(uint8_t period,uint64_t *frequencies,uint8_t frequencies_length,LoRaModule* chip);

int LoRa_rx_get_packet_rssi(LoRaModule* chip, int16_t* rssi); // keep and eye on this one

int LoRa_rx_get_packet_snr(LoRaModule* chip,float *snr);

int LoRa_rx_get_frequency_error(LoRaModule* chip, int32_t *result);

int LoRa_dump_registers(uint8_t* output,LoRaModule* chip);

void LoRa_tx_set_callback(void (*tx_callback)(LoRaModule*),LoRaModule* chip);

int LoRa_tx_set_pa_config(LoRa_pa_pin_t pin,int power,LoRaModule* chip);

int LoRa_tx_set_ocp(bool enable, uint8_t max_current, LoRaModule* chip);

int LoRa_tx_set_explicit_header(LoRa_tx_header_t *header, LoRaModule* chip);

int LoRa_tx_set_for_transmission(const uint8_t* data,uint8_t data_length, LoRaModule* chip);

int LoRa_set_ppm_offset(int32_t frequency_error, LoRaModule* chip);

void LoRa_cad_set_callback(void (*cad_callback)(LoRaModule*, int),LoRaModule* chip);

void handle_interrupt_fromisr(void* arg);

void handle_interrupt_rx_task(void *arg);

void handle_interrupt_tx_task(void* arg);

void LoRa_rx_callback(LoRaModule* chip, uint8_t* data, uint16_t length);

void cad_callback(LoRaModule* chip,int cad_detected);

void setup_gpio_interrupts(gpio_num_t gpio, LoRaModule *device, gpio_int_type_t type);

esp_err_t setup_rx_task(LoRaModule *chip);

esp_err_t setup_tx_task(LoRaModule *chip, void (*tx_callback)(LoRaModule *device));

void updateshit(int seconds);


#endif // LORA_H