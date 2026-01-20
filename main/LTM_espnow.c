#include "LTM_espnow.h"
#include "LEDs.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "freertos/queue.h"

static const char *TAG = "LTM_ESPNOW";

// Global state variables
static LTM_type_t system_mode;
static uint32_t car_number;
static uint8_t paddock_mac[6];
static uint8_t *car_macs = NULL;
static uint8_t car_mac_count = 0;
static gpio_num_t activity_led;
static QueueHandle_t rx_queue = NULL;  // Queue for received packets (non-blocking callback)

// Forward declarations for callbacks
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

/**
 * @brief Initialize NVS flash (required for WiFi)
 */
static esp_err_t nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief Initialize WiFi in Station mode (required before ESP-NOW)
 */
static esp_err_t wifi_init(uint8_t channel, bool long_range_mode) {
    // Create event loop if not already created
    esp_event_loop_create_default();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set WiFi mode to STA (works for both CAR and PADDOCK)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set WiFi channel
    if (channel > 0 && channel <= 13) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    }

    // Enable Long Range mode if requested
    if (long_range_mode) {
        ESP_LOGI(TAG, "Enabling 802.11 LR mode for extended range");
        ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR));
    }

    // Set TX power to maximum (20 dBm = 84 in range 8-84)
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));

    ESP_LOGI(TAG, "WiFi initialized: Channel=%d, LR_Mode=%d", channel, long_range_mode);
    return ESP_OK;
}

// Send callback statistics
static uint32_t send_cb_success_count = 0;
static uint32_t send_cb_fail_count = 0;

/**
 * @brief Send callback - called when ESP-NOW transmission completes
 * Note: This is called AFTER esp_now_send() returns, indicates ACK status
 */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        // Transmission successful (got ACK from peer)
        send_cb_success_count++;
        if (activity_led != GPIO_NUM_NC) {
            LED_off(activity_led);
        }
    } else {
        // Transmission failed (no ACK from peer) - status=1 means ESP_NOW_SEND_FAIL
        send_cb_fail_count++;
        ESP_LOGV(TAG, "ESP-NOW ACK failed to " MACSTR " (status=%d, likely no ACK from peer)",
                 MAC2STR(mac_addr), status);
        if (activity_led != GPIO_NUM_NC) {
            LED_off(activity_led);
        }
    }

    // Log statistics every 500 ACKs
    static uint32_t ack_count = 0;
    ack_count++;
    if (ack_count % 500 == 0) {
        ESP_LOGI(TAG, "ACK Stats: success=%lu, fail=%lu (%.1f%% success rate)",
                 send_cb_success_count, send_cb_fail_count,
                 (float)send_cb_success_count*100.0f/(float)(send_cb_success_count + send_cb_fail_count));
    }
}

// Receive statistics (declared outside callback to persist across calls)
static uint32_t rx_packet_count = 0;
static uint32_t rx_zero_data_count = 0;

/**
 * @brief Receive callback - called when ESP-NOW data is received
 * Note: Called from WiFi task context - keep this fast to prevent watchdog issues
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (activity_led != GPIO_NUM_NC) {
        LED_on(activity_led);
    }
    if (system_mode == CAR) {
        // CAR mode: Parse command and execute
        if (len >= 4) {
            uint32_t *cmd_type_ptr = (uint32_t *)data;
            uint32_t cmd_type = *cmd_type_ptr;

            switch (cmd_type) {
                case ESPNOW_CMD_TYPE_STATUS_REQUEST:
                    ESP_LOGD(TAG, "Received STATUS_REQUEST from paddock");
                    // TODO: Implement status response
                    break;
                case ESPNOW_CMD_TYPE_DATA_REQUEST:
                    ESP_LOGD(TAG, "Received DATA_REQUEST from paddock");
                    // TODO: Implement data response
                    break;
                case ESPNOW_CMD_TYPE_CONFIG_UPDATE:
                    ESP_LOGD(TAG, "Received CONFIG_UPDATE from paddock");
                    // TODO: Implement config update
                    break;
                case ESPNOW_CMD_TYPE_PARAM_TUNE:
                    ESP_LOGD(TAG, "Received PARAM_TUNE from paddock");
                    // TODO: Implement parameter tuning
                    break;
                default:
                    ESP_LOGV(TAG, "Unknown command type: %lu", cmd_type);
                    break;
            }
        }
    } else {
        // PADDOCK mode: Queue telemetry for serial output (non-blocking)
        // Never call serial_send() from callback - it blocks the WiFi task!
        rx_packet_count++;

        // Check if all data is zeros (after car_number)
        if (len > 4) {
            uint32_t *data_ptr = (uint32_t *)(data + 4);
            uint32_t data_sum = 0;
            for (int i = 4; i < len; i += 4) {
                if (i + 4 <= len) {
                    data_sum |= *data_ptr;
                    data_ptr++;
                }
            }
            if (data_sum == 0) {
                rx_zero_data_count++;
                if (rx_zero_data_count % 50 == 1) {
                    ESP_LOGW(TAG, "PADDOCK RX: Received all-zero data from car (len=%d)", len);
                }
            }
        }

        // Queue packet for processing in paddock_ritual task (non-blocking)
        if (rx_queue != NULL && len <= MAX_MSG_LEN) {
            espnow_rx_packet_t rx_pkt;
            memcpy(rx_pkt.data, data, len);
            rx_pkt.length = len;

            // Use non-blocking queue send (0 timeout)
            if (xQueueSendFromISR(rx_queue, &rx_pkt, NULL) != pdTRUE) {
                ESP_LOGV(TAG, "RX queue full, dropping packet (queue likely under high load)");
            }
        }
    }

    if (activity_led != GPIO_NUM_NC) {
        LED_off(activity_led);
    }
}

/**
 * @brief Initialize ESP-NOW wireless communication
 */
int espnow_init(espnow_config_t* config, uint32_t car_num, LTM_type_t ltm_type,
                uint8_t peer_macs[][6], uint8_t peer_count) {

    // Store global parameters
    system_mode = ltm_type;
    car_number = car_num;
    activity_led = config->activity_led;

    // Initialize LED if configured
    if (activity_led != GPIO_NUM_NC) {
        configure_led(activity_led);
        LED_off(activity_led);
    }

    // Step 1: Initialize NVS (required for WiFi)
    ESP_LOGI(TAG, "Initializing NVS flash");
    ESP_ERROR_CHECK(nvs_init());

    // Step 2: Initialize WiFi (required before ESP-NOW)
    ESP_LOGI(TAG, "Initializing WiFi");
    ESP_ERROR_CHECK(wifi_init(config->channel, config->long_range_mode));

    // Step 3: Initialize RX queue for non-blocking serial output (PADDOCK mode)
    if (ltm_type == PADDOCK && rx_queue == NULL) {
        rx_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(espnow_rx_packet_t));
        if (rx_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create RX queue");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "RX queue created (size=%d)", RX_QUEUE_SIZE);
    }

    // Step 4: Initialize ESP-NOW
    ESP_LOGI(TAG, "Initializing ESP-NOW");
    ESP_ERROR_CHECK(esp_now_init());

    // Step 5: Register callbacks
    ESP_LOGI(TAG, "Registering ESP-NOW callbacks");
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // Step 5: Add peers
    if (ltm_type == CAR) {
        // CAR mode: Register paddock as peer
        if (peer_count > 0 && peer_macs != NULL) {
            memcpy(paddock_mac, peer_macs[0], 6);
            ESP_LOGI(TAG, "CAR mode: Registering paddock MAC as peer: %02X:%02X:%02X:%02X:%02X:%02X",
                     paddock_mac[0], paddock_mac[1], paddock_mac[2],
                     paddock_mac[3], paddock_mac[4], paddock_mac[5]);

            // Get our own MAC for verification
            uint8_t own_mac[6];
            esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac);
            ESP_LOGI(TAG, "Device's own MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);

            // Warn if trying to register own MAC as peer
            if (memcmp(paddock_mac, own_mac, 6) == 0) {
                ESP_LOGW(TAG, "WARNING: Paddock MAC matches device's own MAC! Check config file.");
            }

            ESP_ERROR_CHECK(espnow_add_peer(paddock_mac, config->channel, false));
        } else {
            ESP_LOGW(TAG, "CAR mode: No peer MAC configured (peer_count=%d)", peer_count);
        }
    } else {
        // PADDOCK mode: Register all car MACs as peers
        ESP_LOGI(TAG, "PADDOCK mode: Registering %d car MACs as peers", peer_count);
        car_mac_count = peer_count;

        if (peer_count > 0) {
            car_macs = (uint8_t *)malloc(6 * peer_count);
            if (car_macs == NULL) {
                ESP_LOGE(TAG, "Failed to allocate car_macs array");
                return ESP_ERR_NO_MEM;
            }
            memcpy(car_macs, (uint8_t *)peer_macs, 6 * peer_count);

            for (uint8_t i = 0; i < peer_count; i++) {
                uint8_t *car_mac = car_macs + (i * 6);
                ESP_LOGI(TAG, "Adding peer %d: " MACSTR, i, MAC2STR(car_mac));
                ESP_ERROR_CHECK(espnow_add_peer(car_mac, config->channel, false));
            }
        }
    }

    ESP_LOGI(TAG, "ESP-NOW initialization complete");
    return ESP_OK;
}

/**
 * @brief Send ESP-NOW data to a specific peer
 * Note: esp_now_send() returns immediately (async). Status callback is called later.
 */
int espnow_send(const uint8_t *dest_mac, uint8_t* data, size_t length) {
    if (length > MAX_MSG_LEN) {
        ESP_LOGE(TAG, "Data length %zu exceeds max %d", length, MAX_MSG_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    if (activity_led != GPIO_NUM_NC) {
        LED_on(activity_led);
    }

    esp_err_t err = esp_now_send(dest_mac, data, length);
    if (err != ESP_OK) {
        // This is a local error (queue full, peer not found, etc)
        ESP_LOGV(TAG, "esp_now_send() failed locally: %s (peer: " MACSTR ", len=%zu)",
                 esp_err_to_name(err), MAC2STR(dest_mac), length);
        if (activity_led != GPIO_NUM_NC) {
            LED_off(activity_led);
        }
    }
    // Return value indicates if esp_now_send accepted the packet for queuing
    // The actual ACK status comes in the send_cb callback
    return err;
}

/**
 * @brief Send ESP-NOW command to a specific peer (PADDOCK mode)
 */
int espnow_paddock_send_command(const uint8_t *dest_mac, uint32_t cmd_type,
                                uint8_t* payload, size_t payload_len) {
    if (payload_len > (MAX_MSG_LEN - 4)) {
        ESP_LOGE(TAG, "Payload length %zu exceeds max %d", payload_len, MAX_MSG_LEN - 4);
        return ESP_ERR_INVALID_ARG;
    }

    // Build command packet
    espnow_command_packet_t cmd_pkt;
    cmd_pkt.command_type = cmd_type;

    if (payload != NULL && payload_len > 0) {
        memcpy(cmd_pkt.payload, payload, payload_len);
    }

    size_t total_len = 4 + payload_len;
    return espnow_send(dest_mac, (uint8_t *)&cmd_pkt, total_len);
}

/**
 * @brief Add or modify a peer device
 */
int espnow_add_peer(const uint8_t *peer_mac, uint8_t channel, bool encrypt) {
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer->peer_addr, peer_mac, 6);
    peer->channel = channel;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = encrypt;

    esp_err_t err = esp_now_add_peer(peer);
    free(peer);

    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Peer added: " MACSTR " on channel %d", MAC2STR(peer_mac), channel);
    return ESP_OK;
}

/**
 * @brief Remove a peer device
 */
int espnow_remove_peer(const uint8_t *peer_mac) {
    esp_err_t err = esp_now_del_peer(peer_mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove peer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Peer removed: " MACSTR, MAC2STR(peer_mac));
    return ESP_OK;
}

/**
 * @brief CAR mode main task - transmits telemetry at configured frequency
 */
void espnow_car_ritual(void* params) {
    (void)params;

    // Subscribe this task to the watchdog timer for extra safety
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // NULL = current task
    ESP_LOGI(TAG, "CAR ritual subscribed to watchdog");

    TickType_t curr_ticks;
    static uint32_t send_count = 0;
    static uint32_t send_success = 0;
    static uint32_t send_failures = 0;
    static uint32_t zero_data_count = 0;

    while (1) {
        // Reset watchdog timer
        esp_task_wdt_reset();

        curr_ticks = xTaskGetTickCount();

        // Get telemetry data from data service
        uint8_t data[MAX_MSG_LEN];
        uint16_t len;

        esp_err_t err = data_service_get_LoRa_data(data, &len, car_number);

        if (err == ESP_OK) {
            send_count++;

            // Debug: Check if all data is zeros (after car_number)
            uint32_t *data_ptr = (uint32_t *)(data + 4);  // Skip car_number (first 4 bytes)
            uint32_t data_sum = 0;
            for (uint16_t i = 4; i < len; i += 4) {
                if (i + 4 <= len) {
                    data_sum |= *data_ptr;
                    data_ptr++;
                }
            }

            if (data_sum == 0 && len > 4) {
                zero_data_count++;
                if (zero_data_count % 50 == 1) {  // Log every 50 frames
                    ESP_LOGW(TAG, "CAR TX: All data zeros (len=%u) - check CAN input", len);
                }
            }

            // Send to paddock
            esp_err_t send_err = espnow_send(paddock_mac, data, len);

            if (send_err == ESP_OK) {
                send_success++;
            } else {
                send_failures++;
            }

            // Log statistics every 500 frames (~10 seconds at 50Hz)
            if (send_count % 500 == 0) {
                ESP_LOGI(TAG, "CAR TX Stats: sent=%lu, success=%lu, fail=%lu, zero_data=%lu (%.1f%%)",
                         send_count, send_success, send_failures,
                         zero_data_count, (float)zero_data_count*100.0f/(float)send_count);
            }
        } else {
            ESP_LOGW(TAG, "Failed to get LoRa data: %s", esp_err_to_name(err));
        }

        // Delay until next transmission (50Hz = 20ms)
        xTaskDelayUntil(&curr_ticks, pdMS_TO_TICKS(1000 / SEND_FREQUENCY));
    }
}

/**
 * @brief PADDOCK mode main task - processes queued telemetry for serial output
 * Note: Receive callbacks queue data (non-blocking). This task sends to serial.
 */
void espnow_paddock_ritual(void* params) {
    (void)params;

    ESP_LOGI(TAG, "PADDOCK mode active, processing telemetry...");

    // Subscribe this task to the watchdog timer to prevent timeout during long printf operations
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // NULL = current task
    ESP_LOGI(TAG, "PADDOCK ritual subscribed to watchdog");

    espnow_rx_packet_t rx_pkt;
    uint32_t packets_processed = 0;

    // In PADDOCK mode, the receive callbacks queue data (fast, non-blocking)
    // This task processes the queue and sends to serial (can block)
    while (1) {
        // Reset watchdog timer to prevent timeout
        esp_task_wdt_reset();

        // Try to get a packet from queue with timeout
        if (xQueueReceive(rx_queue, &rx_pkt, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Process the packet: send to serial
            // This can block, so we feed the watchdog before and after
            serial_send(rx_pkt.data, rx_pkt.length);
            packets_processed++;

            // Feed watchdog again after potentially long printf operation
            esp_task_wdt_reset();

            // Yield to allow IDLE task to run (critical for watchdog)
            taskYIELD();
        } else {
            // Queue empty - log statistics periodically
            static uint32_t last_count = 0;
            if (rx_packet_count > last_count && rx_packet_count % 500 == 0) {
                ESP_LOGI(TAG, "PADDOCK RX Stats: packets=%lu, zero_data=%lu (%.1f%%), processed=%lu",
                         rx_packet_count, rx_zero_data_count,
                         (float)rx_zero_data_count*100.0f/(float)rx_packet_count,
                         packets_processed);
                last_count = rx_packet_count;
            }

            // Yield to IDLE task during idle periods
            taskYIELD();
        }
    }
}
