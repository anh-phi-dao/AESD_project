# Depends on mqtt_client but never on lock_service: nothing received over MQTT may reach the lock (docs/mqtt_protocol.md).
idf_component_register(SRCS "remote_service.c" "remote_service_json.c"
                       INCLUDE_DIRS "."
                       REQUIRES event_log mqtt_client util freertos
                       PRIV_REQUIRES json log)
