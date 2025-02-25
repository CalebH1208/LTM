idf_component_register(SRCS "LTM_init.c" "LTM_CAN.c" "LTM_data_logging.c" "LTM_data_service.c" "LTM_LoRa.c" "Spi.c" "LoRa.c" "main.c" "LEDs.c" 
                    PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi fatfs json driver
                    INCLUDE_DIRS ""
                    )
