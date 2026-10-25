# Local screen UI, drawn in two SquareLine Studio projects (docs/tft_ui.md):
#   design/squareline/SmartLock_UI.spj             -> ui/        user screens (Home, Pin, Voice, ...)
#   design/squareline_admin/SmartLock_adminUI.spj  -> ui_admin/  admin screens (AdminLogin, AdminMenu, ...)
# The CMakeLists.txt files SquareLine writes are not used: every .c file under both folders is compiled here, and
# CONFIGURE_DEPENDS re-globs on each build, so adding screens or fonts in SquareLine needs no change in this file.
# Without a user export, a built-in test screen is compiled instead and ui_admin/ is ignored.
if(CMAKE_SCRIPT_MODE_FILE)
    # ESP-IDF first reads this file in script mode to collect requirements; CONFIGURE_DEPENDS is invalid there.
    file(GLOB_RECURSE ui_export_srcs "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
    file(GLOB_RECURSE ui_admin_export_srcs "${CMAKE_CURRENT_LIST_DIR}/ui_admin/*.c")
else()
    file(GLOB_RECURSE ui_export_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
    file(GLOB_RECURSE ui_admin_export_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/ui_admin/*.c")
endif()

set(srcs "ui_service.c")
set(priv_include_dirs "")
set(has_squareline 0)
set(has_admin_ui 0)
if(ui_export_srcs)
    list(APPEND srcs ${ui_export_srcs})
    list(APPEND priv_include_dirs "ui")
    set(has_squareline 1)
else()
    list(APPEND srcs "ui_service_fallback.c")
endif()

# Both exports define the same global names. The admin copy of each shared file is dropped so the firmware links:
#   ui_helpers.c, components/*  identical in both exports (same SquareLine version); the user export's copy is used.
#   fonts/*, images/*           a file with the same name in ui/ is the same font or image; one copy is enough.
#   ui.c                        kept for admin animations; ui_init/ui_destroy are renamed (see below), and the
#                               firmware never calls them: ui_service_admin.c opens AdminLogin itself.
if(ui_export_srcs AND ui_admin_export_srcs)
    set(ui_admin_srcs "")
    foreach(src IN LISTS ui_admin_export_srcs)
        file(RELATIVE_PATH rel "${CMAKE_CURRENT_LIST_DIR}/ui_admin" "${src}")
        if(rel STREQUAL "ui_helpers.c" OR rel MATCHES "^components/")
            continue()
        endif()
        if(rel MATCHES "^(fonts|images)/" AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/ui/${rel}")
            continue()
        endif()
        list(APPEND ui_admin_srcs "${src}")
    endforeach()
    list(APPEND srcs ${ui_admin_srcs} "ui_service_admin.c")
    set(has_admin_ui 1)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES lvgl_port lvgl util
                       PRIV_REQUIRES log)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
                           UI_SERVICE_HAS_SQUARELINE=${has_squareline}
                           UI_SERVICE_HAS_ADMIN_UI=${has_admin_ui})

# Generated code is overwritten on every export, so its warnings must not break the build.
if(ui_export_srcs)
    set_source_files_properties(${ui_export_srcs} PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-variable")
endif()
if(has_admin_ui)
    set_source_files_properties(${ui_admin_srcs} PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-variable")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui_admin/ui.c" PROPERTIES COMPILE_DEFINITIONS
                                "ui_init=ui_admin_init;ui_destroy=ui_admin_destroy;ui____initial_actions0=ui_admin____initial_actions0")
endif()
