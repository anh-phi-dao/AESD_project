idf_component_register(SRCS "network_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event esp_timer util
                       PRIV_REQUIRES log)
