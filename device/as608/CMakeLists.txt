idf_component_register(SRCS "fingerprint_sensor.c" "as608.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos UART)
