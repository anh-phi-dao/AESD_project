# Checks temporary guest codes offline from a shared secret. Never depends on MQTT: codes are typed on the keypad.
idf_component_register(SRCS "temp_code.c"
                       INCLUDE_DIRS "."
                       REQUIRES util freertos
                       PRIV_REQUIRES mbedtls)
