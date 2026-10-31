cmake_minimum_required(VERSION 3.16)

# Component AS608 nằm trong device/, UART nằm trong BSP/
set(EXTRA_COMPONENT_DIRS
    "../../device/as608"
    "../../BSP/UART"
)

set(COMPONENTS main as608 UART esptool_py)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_fingerprint_sensor_implement)
