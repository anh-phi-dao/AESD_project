# Dev-board simulator. Built always, started only when CONFIG_DOOR_SIM_ENABLE is set (main/Kconfig.projbuild).
idf_component_register(SRCS "door_sim.c"
                       INCLUDE_DIRS "."
                       REQUIRES event_log util freertos esp_driver_gpio
                       PRIV_REQUIRES console esp_hw_support log)
