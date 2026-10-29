set(DEVICE_REQUIRES keypad pcf8574 audio_codec ili9341_tft_display pn532 sd_card)
set(APP_REQUIRES auth_manager device_config network_manager mqtt_client event_log remote_service door_sim lvgl_port ui_service)
idf_component_register(SRCS "ui_flow_task.c" "network_task.c" "keypad_task.c" "ui_task.c"
                       INCLUDE_DIRS "."
                       REQUIRES util driver ${DEVICE_REQUIRES} ${APP_REQUIRES})