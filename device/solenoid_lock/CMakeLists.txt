idf_component_register(SRCS "solenoid_lock.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos esp_timer util)
