idf_component_register(SRCS "system_control.c"
                       INCLUDE_DIRS "."
                       REQUIRES util driver keypad auth_manager)