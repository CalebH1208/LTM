idf_component_register(SRCS "LTM_espnow.c" "LTM_serial_service.c" "LTM_init.c" "LTM_CAN.c" "LTM_data_logging.c" "LTM_data_service.c" "main.c" "LEDs.c"
                    PRIV_REQUIRES spi_flash esp_driver_gpio fatfs json driver nvs_flash esp_wifi
                    INCLUDE_DIRS ""
                    )
