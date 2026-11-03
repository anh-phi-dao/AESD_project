idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ina219 power_manager util driver esp_timer freertos)
