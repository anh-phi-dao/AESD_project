idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES keypad pcf8574 driver freertos nvs_flash json
                                device_config network_manager mqtt_client)
