set(DEVICE_REQUIRES keypad pcf8574 audio_codec ili9341_tft_display pn532 sd_card)
set(APP_REQUIRES auth_manager)
idf_component_register(SRCS "keypad_task.c"
                       INCLUDE_DIRS "."
                       REQUIRES util driver ${DEVICE_REQUIRES} ${APP_REQUIRES})