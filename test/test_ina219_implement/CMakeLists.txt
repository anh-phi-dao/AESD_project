cmake_minimum_required(VERSION 3.16)

# INA219 driver from device/, SOC estimation from app/power_manager, app_err_t from util/: the same code as the lock.
set(EXTRA_COMPONENT_DIRS
    "../../device/ina219"
    "../../app/power_manager"
    "../../util"
)

set(COMPONENTS main ina219 power_manager util esptool_py)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_ina219_implement)
