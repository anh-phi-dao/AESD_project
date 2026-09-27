# Header files are named mqtt_conn.* because ESP-IDF's mqtt component already provides mqtt_client.h.
idf_component_register(SRCS "mqtt_conn.c"
                       INCLUDE_DIRS "."
                       REQUIRES mqtt util
                       PRIV_REQUIRES mbedtls heap log)
