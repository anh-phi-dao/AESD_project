# Local screen UI, drawn in one SquareLine Studio project (docs/tft_ui.md):
#   design/squareline/SmartLock_UI.spj  -> ui/  screens Home, Input, Voice, AdminLogin, Menu, Status, Overlay
# The CMakeLists.txt SquareLine writes is not used: every .c file under ui/ is compiled here, and CONFIGURE_DEPENDS
# re-globs on each build, so adding screens or fonts in SquareLine needs no change in this file.
# Without an export, a built-in test screen is compiled instead.
if(CMAKE_SCRIPT_MODE_FILE)
    # ESP-IDF first reads this file in script mode to collect requirements; CONFIGURE_DEPENDS is invalid there.
    file(GLOB_RECURSE ui_export_srcs "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
else()
    file(GLOB_RECURSE ui_export_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
endif()

set(srcs "ui_service.c")
set(priv_include_dirs "")
set(has_squareline 0)
if(ui_export_srcs)
    list(APPEND srcs ${ui_export_srcs})
    list(APPEND priv_include_dirs "ui")
    set(has_squareline 1)
else()
    list(APPEND srcs "ui_service_fallback.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES lvgl_port lvgl util
                       PRIV_REQUIRES log)

target_compile_definitions(${COMPONENT_LIB} PRIVATE UI_SERVICE_HAS_SQUARELINE=${has_squareline})

# Generated code is overwritten on every export, so its warnings must not break the build.
if(ui_export_srcs)
    set_source_files_properties(${ui_export_srcs} PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-variable")
endif()
