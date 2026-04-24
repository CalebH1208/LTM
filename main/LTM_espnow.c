#include "LTM_espnow.h"
#include "LEDs.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "LTM_LapTimer.h"

// #include "esp_wifi_types.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/queue.h"

static const char *TAG = "LTM_ESPNOW";

// Global state variables
static LTM_type_t system_mode;
static uint32_t car_number;
static uint8_t paddock_mac[6];
static uint8_t *car_macs = NULL;
static uint8_t *lt_macs = NULL;
static lt_state_t *lt_states = NULL;
static uint8_t car_mac_count = 0;
static uint8_t lt_mac_count = 0;
static gpio_num_t activity_led;
static QueueHandle_t rx_queue = NULL;  // Queue for received packets (non-blocking callback)
static laptimer_t *lt_times = NULL;

// Send buffer stack (CAR mode)
// Circular buffer used as LIFO: push to write_index, pop from (write_index-1).
// Drop-oldest policy when full: write_index overwrites the oldest slot.
static uint8_t*           s_stack_buffer   = NULL;
static uint32_t           s_slot_size      = 0;   // bytes per slot = original_len + 4 (timestamp)
static uint32_t           s_original_len   = 0;   // packet size without timestamp
static int32_t            s_write_index    = 0;   // next push writes here [0, MAX_STACK_DEPTH)
static int32_t            s_count          = 0;   // valid entries [0, MAX_STACK_DEPTH]
static SemaphoreHandle_t  s_stack_mutex    = NULL;
static StaticSemaphore_t  s_stack_mutex_buf;

// ACK feedback from espnow_send_cb to espnow_car_ritual.
// The send task waits on s_ack_sem after each send; the callback gives it and records the result.
static SemaphoreHandle_t  s_ack_sem        = NULL;
static StaticSemaphore_t  s_ack_sem_buf;
static volatile bool      s_last_ack_ok    = false;

// Marker pending flag: set by espnow_recv_cb (WiFi task) when MARKER command arrives.
// Consumed by espnow_enqueue_task (Core 0) to trigger data_service_trigger_marker().
static volatile bool      s_marker_pending = false;

// Outbound marker command from serial_rx_task to espnow_paddock_ritual.
// serial_rx_task sets s_paddock_marker_mac and s_paddock_marker_pending;
// espnow_paddock_ritual calls esp_now_send from its own context (avoiding concurrent send).
static volatile bool      s_paddock_marker_pending = false;
static uint8_t            s_paddock_marker_mac[6]  = {0};

// Forward declarations for callbacks
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

void espnow_paddock_queue_marker(const uint8_t *dest_mac) {
    memcpy(s_paddock_marker_mac, dest_mac, 6);
    s_paddock_marker_pending = true;
}

// ---------------------------------------------------------------------------
// Send buffer stack implementation
// ---------------------------------------------------------------------------

esp_err_t espnow_buffer_init(uint32_t packet_data_len) {
    if (packet_data_len > (MAX_MSG_LEN - sizeof(uint32_t))) {
        ESP_LOGE(TAG, "packet_data_len %lu exceeds max %d (must leave 4 bytes for timestamp)",
                 packet_data_len, MAX_MSG_LEN - (int)sizeof(uint32_t));
        return ESP_ERR_INVALID_ARG;
    }

    s_original_len = packet_data_len;
    s_slot_size    = packet_data_len + sizeof(uint32_t);

    s_stack_buffer = (uint8_t*)malloc((size_t)MAX_STACK_DEPTH * s_slot_size);
    if (s_stack_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate send buffer (%lu bytes)",
                 (uint32_t)MAX_STACK_DEPTH * s_slot_size);
        return ESP_ERR_NO_MEM;
    }

    s_write_index = 0;
    s_count       = 0;

    s_stack_mutex = xSemaphoreCreateMutexStatic(&s_stack_mutex_buf);
    if (s_stack_mutex == NULL) {
        free(s_stack_buffer);
        s_stack_buffer = NULL;
        ESP_LOGE(TAG, "Failed to create stack mutex");
        return ESP_ERR_NO_MEM;
    }

    // Binary semaphore for ACK feedback: starts empty, send_cb gives it after each TX
    s_ack_sem = xSemaphoreCreateBinaryStatic(&s_ack_sem_buf);
    if (s_ack_sem == NULL) {
        free(s_stack_buffer);
        s_stack_buffer = NULL;
        ESP_LOGE(TAG, "Failed to create ACK semaphore");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Send buffer initialized: %d slots x %lu bytes = %lu bytes total",
             MAX_STACK_DEPTH, s_slot_size, (uint32_t)MAX_STACK_DEPTH * s_slot_size);
    return ESP_OK;
}

/**
 * @brief Push a timestamped packet onto the stack (O(1), drop-oldest on overflow).
 * @param data Original packet data (length = s_original_len, no timestamp).
 * @return true on success, false if mutex times out.
 */
static bool stack_push(const uint8_t* data) {
    if (s_stack_buffer == NULL) return false;

    if (xSemaphoreTake(s_stack_mutex, pdMS_TO_TICKS(STACK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "stack_push: mutex timeout");
        return false;
    }

    uint8_t* slot = s_stack_buffer + ((size_t)s_write_index * s_slot_size);

    uint32_t ts_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    memcpy(slot, &ts_ms, sizeof(uint32_t));
    memcpy(slot + sizeof(uint32_t), data, s_original_len);

    s_write_index = (s_write_index + 1) % MAX_STACK_DEPTH;
    if (s_count < MAX_STACK_DEPTH) {
        s_count++;
    }
    // If full, count stays at MAX_STACK_DEPTH and oldest slot is silently overwritten.

    xSemaphoreGive(s_stack_mutex);
    return true;
}

/**
 * @brief Push a pre-timestamped slot back onto the stack (used on ACK failure to re-queue).
 *        The data pointer must point to a full slot (s_slot_size bytes, timestamp already present).
 */
static bool stack_push_raw(const uint8_t* slot_data) {
    if (s_stack_buffer == NULL) return false;

    if (xSemaphoreTake(s_stack_mutex, pdMS_TO_TICKS(STACK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    uint8_t* slot = s_stack_buffer + ((size_t)s_write_index * s_slot_size);
    memcpy(slot, slot_data, s_slot_size);

    s_write_index = (s_write_index + 1) % MAX_STACK_DEPTH;
    if (s_count < MAX_STACK_DEPTH) {
        s_count++;
    }

    xSemaphoreGive(s_stack_mutex);
    return true;
}

/**
 * @brief Pop the newest packet from the stack (O(1), LIFO).
 * @param out_buf Caller buffer of at least s_slot_size bytes.
 *                Contains [timestamp_ms (4B)][original packet (s_original_len)] on success.
 * @param out_len Set to s_slot_size on success.
 * @return true on success, false if stack empty or mutex times out.
 */
static bool stack_pop(uint8_t* out_buf, uint16_t* out_len) {
    if (s_stack_buffer == NULL) return false;

    if (xSemaphoreTake(s_stack_mutex, pdMS_TO_TICKS(STACK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "stack_pop: mutex timeout");
        return false;
    }

    if (s_count == 0) {
        xSemaphoreGive(s_stack_mutex);
        return false;
    }

    int32_t top = (s_write_index - 1 + MAX_STACK_DEPTH) % MAX_STACK_DEPTH;
    memcpy(out_buf, s_stack_buffer + ((size_t)top * s_slot_size), s_slot_size);
    *out_len = (uint16_t)s_slot_size;

    s_write_index = top;
    s_count--;

    xSemaphoreGive(s_stack_mutex);
    return true;
}

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
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (tx_info->tx_status == WIFI_SEND_SUCCESS) {
        send_cb_success_count++;
        s_last_ack_ok = true;
        if (activity_led != GPIO_NUM_NC) {
            LED_off(activity_led);
        }
    } else {
        send_cb_fail_count++;
        s_last_ack_ok = false;
        if (activity_led != GPIO_NUM_NC) {
            LED_off(activity_led);
        }
    }

    // Signal the send task that the ACK result is ready
    if (s_ack_sem != NULL) {
        xSemaphoreGiveFromISR(s_ack_sem, NULL);
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
                case ESPNOW_CMD_TYPE_MARKER:
                    ESP_LOGD(TAG, "Received MARKER command from paddock");
                    s_marker_pending = true;
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
        static uint8_t lap = 1; // Track current lap for Lap Timer packets (car_num == -1), set to -1 so it becomoes 0 when main is first passed

        // Check if all data values are zeros (skip timestamp(4) and car_number(4) = first 8 bytes)
        if (len > 8) {
            uint32_t *data_ptr = (uint32_t *)(data + 8);
            uint32_t data_sum = 0;
            for (int i = 8; i < len; i += 4) {
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

        // Queue packet for processing in paddock_ritual task (non-blocking).
        // All packets from the car include a 4-byte timestamp prefix (always stripped here).
        // Minimum valid packet: [timestamp(4)][car_num(4)] = 8 bytes.
        if (rx_queue != NULL && len >= (int)(2 * sizeof(uint32_t)) && len <= MAX_MSG_LEN) {
            espnow_rx_packet_t rx_pkt = {0};

            //Extra Special Stuff for the LapTimers (car_num == -1)
            if(*(uint32_t*)(data+sizeof(uint32_t)) == -1)  {//Checks if car_number is -1
                memcpy(rx_pkt.data, data + sizeof(uint32_t), 4);

                int pos = -1;

                for(int i = 0; i < lt_mac_count; i++) {
                    if(memcmp(recv_info->src_addr, lt_states[i].mac, 6) == 0) pos = i;
                }
                
                if(pos == -1) {
                    // ESP_LOGE(TAG, "No matching LT MAC found for received packet!");
                    return; // Drop packet if no matching LT found
                }
                // if(pos == 0) lap++;
                
                if(compose_LT_data(data) != ESP_OK) {
                    return; // Drop packet if LT data composition fails (e.g. invalid segment time)
                } 
                lap++;
                memcpy(rx_pkt.data + 4, data, 4);
                memcpy(rx_pkt.data + 8, &pos, sizeof(uint8_t)); //Copy the segment position after the segment_time data
                memcpy(rx_pkt.data + 9, &lap, sizeof(uint8_t)); //Add the lap number
            
                rx_pkt.length = (uint16_t)(10); 
            } else {
                // Strip the leading 4-byte timestamp; remainder is the original [car_num][data...] packet
                memcpy(&rx_pkt.timestamp_ms, data, sizeof(uint32_t));
                memcpy(rx_pkt.data, data + sizeof(uint32_t), len - sizeof(uint32_t));

                rx_pkt.length = (uint16_t)(len - sizeof(uint32_t)); 
            }

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
                uint8_t peer_macs[][6], uint8_t peer_count, uint8_t car_peer_count, lt_array_t LT_array) {
    lt_states = LT_array.lts; // Store pointer to LT States for later use in receive callback
    uint8_t lt_peer_count = LT_array.num_lts;
    if(peer_count > 20) {
        // ESP_LOGW(TAG, "peer_count %d exceeds max 20, truncating to 20", peer_count);
        peer_count = 20;
    }
    // if(peer_count > (lt_peer_count + car_peer_count)) {
    //     ESP_LOGW(TAG, "peer_count: '%d' is greater than lt_peer_count: '%d' + car_peer_count: '%d', so some peers may be ignored",
    //              peer_count, lt_peer_count, car_peer_count);
    // }
    // if (peer_count < (lt_peer_count + car_peer_count)) {
    //     ESP_LOGW(TAG, "peer_count: '%d' is less than lt_peer_count: '%d' + car_peer_count: '%d', so some peers may be missing",
    //              peer_count, lt_peer_count, car_peer_count);
    // }
    
    // Store global parameters
    system_mode = ltm_type;
    car_number = car_num;
    activity_led = config->activity_led;
    car_mac_count = car_peer_count;
    lt_mac_count = lt_peer_count;

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
        // PADDOCK mode: Register all car MACs and Lap Timer MAC as peers 
        ESP_LOGI(TAG, "PADDOCK mode: Registering %d car MACs as peers, %d LT MACS as peers, %d Total_Peers", car_peer_count, lt_peer_count, peer_count);

        if (peer_count > 0) {
            if(lt_peer_count > 0) {
                //Lap Timer MACs
                lt_macs = (uint8_t *)malloc(6 * lt_peer_count);
                if (lt_macs == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate lt_macs array");
                    return ESP_ERR_NO_MEM;
                }
                
                memcpy(lt_macs, (uint8_t *)peer_macs[0], 6 * lt_peer_count); //First 'lt_peer_count' MACs are for Lap Timers

        
                for (uint8_t i = 0; i < lt_peer_count; i++) {
                    uint8_t* lt_mac = lt_macs + (i * 6);
                    ESP_LOGI(TAG, "Adding peer %d: " MACSTR, i, MAC2STR(lt_mac));
                    ESP_ERROR_CHECK(espnow_add_peer(lt_mac, config->channel, false));
                }

                lap_timer_init(); //Initialize the lap timer after the LT MACs have been added
            }

            //Car Macs
            /**
             * Account for the cases stated at the top: Prioritizing LT_Macs since they are probably more important 
             *  in the case when we are actually using them versus other cars
             * 
             * new_car_mac will be peer_count - lt_peer_count to account for the case of there being less peers than lt_peer_count + car_peer_count
             * 
             * The case with more peers than lt_peer_count and car_peer_count is handled automatically by how the code works
             */
            // uint8_t new_car_mac = peer_count - lt_peer_count; 
            // car_mac_count = new_car_mac;

            car_macs = (uint8_t *)malloc(6 * (car_peer_count));
            if (car_macs == NULL) {
                ESP_LOGE(TAG, "Failed to allocate car_macs array");
                return ESP_ERR_NO_MEM;
            }
            memcpy(car_macs, (uint8_t *)peer_macs[lt_peer_count], 6 * car_peer_count); //Next 'new_car_mac' MACS are for the Cars

            for (uint8_t i = 0; i < car_peer_count; i++) {
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
 * @brief 10Hz enqueue task. Samples data_service and pushes timestamped packets onto the stack.
 */
void espnow_enqueue_task(void* params) {
    (void)params;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_LOGI(TAG, "Enqueue task started at %dHz", ENQUEUE_FREQUENCY);

    uint8_t    data[MAX_MSG_LEN];
    uint16_t   len;
    TickType_t curr_ticks;

    while (1) {
        esp_task_wdt_reset();
        curr_ticks = xTaskGetTickCount();

        if (s_marker_pending) {
            s_marker_pending = false;
            data_service_trigger_marker();
        }

        if (data_service_get_LoRa_data(data, &len, car_number) == ESP_OK) {
            stack_push(data);
        } else {
            ESP_LOGW(TAG, "Enqueue: data_service_get_LoRa_data failed");
        }

        xTaskDelayUntil(&curr_ticks, pdMS_TO_TICKS(1000 / ENQUEUE_FREQUENCY));
    }
}

/**
 * @brief CAR mode main task - drains send buffer stack at 30Hz, newest packet first.
 *        Waits for ACK after each send. On ACK failure, pushes the packet back so it
 *        is retried and the stack accumulates during outages.
 */
void espnow_car_ritual(void* params) {
    (void)params;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_LOGI(TAG, "CAR ritual subscribed to watchdog, sending at %dHz", SEND_FREQUENCY);

    // ACK timeout: ESP-NOW round-trip is typically <10ms. Allow 80ms max before
    // treating as a failure (covers worst-case retransmit + channel congestion).
    // This is safely within the 33ms tick period — if the ACK takes longer than
    // the tick we simply miss that tick's delay, which is acceptable.
    const TickType_t ACK_TIMEOUT = pdMS_TO_TICKS(80);

    TickType_t curr_ticks;
    static uint32_t send_count    = 0;
    static uint32_t send_success  = 0;
    static uint32_t send_failures = 0;
    static uint32_t pushback_count = 0;

    while (1) {
        esp_task_wdt_reset();
        curr_ticks = xTaskGetTickCount();

        uint8_t  tx_buf[MAX_MSG_LEN];
        uint16_t tx_len = 0;

        if (stack_pop(tx_buf, &tx_len)) {
            // tx_buf = [timestamp_ms (4B)][car_num (4B)][data_0 (4B)]...[data_N (4B)]
            esp_err_t send_err = espnow_send(paddock_mac, tx_buf, tx_len);
            send_count++;

            if (send_err == ESP_OK) {
                // Wait for ACK callback to signal the result
                if (xSemaphoreTake(s_ack_sem, ACK_TIMEOUT) == pdTRUE) {
                    if (s_last_ack_ok) {
                        send_success++;
                    } else {
                        // Paddock didn't ACK — link is down, push packet back onto stack
                        send_failures++;
                        pushback_count++;
                        stack_push_raw(tx_buf);
                    }
                } else {
                    // ACK timed out — treat as failure, push back
                    send_failures++;
                    pushback_count++;
                    stack_push_raw(tx_buf);
                }
            } else {
                // esp_now_send itself failed locally (peer not found, queue full, etc.)
                send_failures++;
                pushback_count++;
                stack_push_raw(tx_buf);
            }

            // Log statistics every 300 frames (~10 seconds at 30Hz)
            if (send_count % 300 == 0) {
                ESP_LOGI(TAG, "CAR TX: sent=%lu ok=%lu fail=%lu pushback=%lu depth=%ld",
                         send_count, send_success, send_failures, pushback_count, (long)s_count);
            }
        }
        // Stack empty — skip this tick, delay still fires on schedule.

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

        // Send outbound marker command if queued by serial_rx_task.
        // Must be called from THIS task to avoid concurrent esp_now_send calls.
        if (s_paddock_marker_pending) {
            s_paddock_marker_pending = false;
            uint8_t dummy = 0;
            espnow_paddock_send_command(s_paddock_marker_mac, ESPNOW_CMD_TYPE_MARKER, &dummy, 0);
        }

        // Try to get a packet from queue with timeout
        if (xQueueReceive(rx_queue, &rx_pkt, pdMS_TO_TICKS(100)) == pdTRUE) {
            serial_send(rx_pkt.data, rx_pkt.length, rx_pkt.timestamp_ms);
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
