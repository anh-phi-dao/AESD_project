idf_component_register(SRCS "password.c" "uid.c"
                       INCLUDE_DIRS "."
                       REQUIRES util keypad driver storage_manager)