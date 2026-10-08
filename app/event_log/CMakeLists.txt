idf_component_register(SRCS "event_log.c"
                       INCLUDE_DIRS "."
                       REQUIRES util freertos
                       PRIV_REQUIRES nvs_flash esp_hw_support log)
