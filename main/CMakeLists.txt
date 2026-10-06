set(DEVICE_REQUIRES keypad pcf8574 audio_codec ili9341_tft_display pn532 sd_card)
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos nvs_flash json
                                device_config network_manager mqtt_client ${DEVICE_REQUIRES})
