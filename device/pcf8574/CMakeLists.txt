idf_component_register(SRCS "pcf8574.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos esp_timer util)
    
# Enable code coverage instrumentation for PCF8574 driver
target_compile_options(${COMPONENT_LIB} PRIVATE --coverage)
target_link_libraries(${COMPONENT_LIB} PRIVATE --coverage)
