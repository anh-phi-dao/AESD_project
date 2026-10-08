idf_component_register(SRCS "ili9341.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos esp_timer SPI lvgl)