idf_component_register(SRCS
                       INCLUDE_DIRS "."
                       REQUIRES event_log mqtt_client util freertos
                       PRIV_REQUIRES json log)