#ifndef LTM_ESPNOW_H
#define LTM_ESPNOW_H

#include "shared.h"
#include "LTM_data_service.h"
#include "LTM_serial_service.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"

#define MAX_MSG_LEN 250  // ESP-NOW v1.0 max payload
#define RX_QUEUE_SIZE 20   // Queue for received packets (non-blocking callback)

#define SEND_FREQUENCY 50  // 50Hz transmission rate

// WiFi and ESP-NOW configuration
typedef struct {
    wifi_mode_t wifi_mode;           // WIFI_MODE_STA for both CAR and PADDOCK
    uint8_t channel;                  // WiFi channel 1-13 (from JSON WC field)
    bool long_range_mode;             // Enable 802.11 LR mode
    gpio_num_t activity_led;          // LED GPIO for activity indication
} espnow_config_t;

// Peer information structure for MAC address storage
typedef struct {
    uint8_t mac[6];                  // MAC address (6 bytes)
    uint8_t channel;                 // WiFi channel
    bool encrypt;                    // Encryption enabled
} espnow_peer_t;

// Received packet queue structure (for non-blocking serial output)
typedef struct {
    uint8_t data[MAX_MSG_LEN];       // Packet data
    uint16_t length;                 // Data length
} espnow_rx_packet_t;

// Command packet structures for bidirectional communication
typedef enum {
    ESPNOW_CMD_TYPE_STATUS_REQUEST = 1,
    ESPNOW_CMD_TYPE_DATA_REQUEST = 2,
    ESPNOW_CMD_TYPE_CONFIG_UPDATE = 3,
    ESPNOW_CMD_TYPE_PARAM_TUNE = 4,
    ESPNOW_CMD_TYPE_ACK = 5
} espnow_command_type_t;

typedef struct {
    uint32_t command_type;           // Command type (espnow_command_type_t)
    uint8_t payload[MAX_MSG_LEN - 4];  // Remaining space for command data
} espnow_command_packet_t;

// Function declarations

/**
 * @brief Initialize ESP-NOW wireless communication
 * @param config Pointer to espnow_config_t configuration structure
 * @param car_num Car number identifier (used in CAR mode)
 * @param ltm_type System mode (CAR or PADDOCK)
 * @param peer_macs Array of peer MAC addresses to register
 * @param peer_count Number of peers to register
 * @return ESP_OK on success, error code on failure
 */
int espnow_init(espnow_config_t* config, uint32_t car_num, LTM_type_t ltm_type,
                uint8_t peer_macs[][6], uint8_t peer_count);

/**
 * @brief Send ESP-NOW data to a specific peer
 * @param dest_mac Destination MAC address (6 bytes)
 * @param data Pointer to data buffer
 * @param length Data length in bytes (max 250)
 * @return ESP_OK on success, error code on failure
 */
int espnow_send(const uint8_t *dest_mac, uint8_t* data, size_t length);

/**
 * @brief Send ESP-NOW command to a specific peer (PADDOCK mode)
 * @param dest_mac Destination MAC address (6 bytes)
 * @param cmd_type Command type (espnow_command_type_t)
 * @param payload Command payload data
 * @param payload_len Length of payload (max 246 bytes)
 * @return ESP_OK on success, error code on failure
 */
int espnow_paddock_send_command(const uint8_t *dest_mac, uint32_t cmd_type,
                                uint8_t* payload, size_t payload_len);

/**
 * @brief CAR mode main task - transmits telemetry at configured frequency
 * @param params Task parameters (unused)
 */
void espnow_car_ritual(void* params);

/**
 * @brief PADDOCK mode main task - waits for telemetry and commands
 * @param params Task parameters (unused)
 */
void espnow_paddock_ritual(void* params);

/**
 * @brief Add or modify a peer device
 * @param peer_mac MAC address of peer (6 bytes)
 * @param channel WiFi channel
 * @param encrypt Enable encryption for this peer
 * @return ESP_OK on success, error code on failure
 */
int espnow_add_peer(const uint8_t *peer_mac, uint8_t channel, bool encrypt);

/**
 * @brief Remove a peer device
 * @param peer_mac MAC address of peer (6 bytes)
 * @return ESP_OK on success, error code on failure
 */
int espnow_remove_peer(const uint8_t *peer_mac);

#endif // LTM_ESPNOW_H
