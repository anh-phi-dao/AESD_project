idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES keypad pcf8574 driver freertos auth_manager)
idf_build_set_property(COMPILE_OPTIONS "-DDEBUG_PASSWORD" APPEND)
