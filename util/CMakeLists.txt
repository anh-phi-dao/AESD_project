idf_component_register(SRCS "err_map.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_common)
target_compile_options(${COMPONENT_LIB} PRIVATE --coverage)
target_link_libraries(${COMPONENT_LIB} PRIVATE --coverage)
add_link_options(--coverage)
