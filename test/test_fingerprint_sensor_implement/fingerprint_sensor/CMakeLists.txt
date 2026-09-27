idf_component_register(SRCS "fingerprint_sensor.c" "ili9341.c"
                            "../ili9341_lib_build.c"
                       INCLUDE_DIRS "." ".."
                       REQUIRES driver freertos esp_timer SPI lvgl)