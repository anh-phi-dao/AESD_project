set(DEVICE_REQUIRES keypad 
                    pcf8574 
                    audio_codec 
                    ili9341_tft_display 
                    pn532 sd_card)
set(APP_REQUIRES auth_manager 
                 system_controller
                 device_config 
                 network_manager
                 lvgl_port 
                 mqtt_client 
                 event_log 
                 remote_service
                 ui_service
                 door_sim)
idf_component_register(SRCS "system_task.c" 
                            "rfid_task.c" 
                            "network_task.c" 
                            "keypad_task.c"
                            "system_task.c"
                            "ui_task.c"
                            "audio_task.c"
                       INCLUDE_DIRS "."
                       REQUIRES util driver ${DEVICE_REQUIRES} ${APP_REQUIRES})
