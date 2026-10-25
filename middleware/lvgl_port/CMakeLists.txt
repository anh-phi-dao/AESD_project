# Runs LVGL 8 (middleware/lvgl) on FreeRTOS. Display-agnostic: the flush callback is supplied by the caller.
idf_component_register(SRCS "lvgl_port.c"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl util freertos esp_timer
                       PRIV_REQUIRES heap log)
