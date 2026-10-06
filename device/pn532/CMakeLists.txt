
set(PN532_OBJ "pn532.c" "pn532_driver.c" "pn532_driver_spi.c" "pn532_driver_i2c.c" "pn532_driver_hsu.c")
idf_component_register(SRCS ${PN532_OBJ}
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sdmmc driver unity freertos esp_timer)