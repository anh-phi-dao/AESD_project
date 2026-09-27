idf_component_register(SRCS "sd_card.c"
                            "../sd_card_lib_build.c"
                       INCLUDE_DIRS "." ".."
                       REQUIRES fatfs sdmmc driver unity freertos esp_timer)