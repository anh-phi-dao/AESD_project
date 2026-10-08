# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "main" "../../app" "../../device" "../../middleware" "../../util" "../../third_party")
set(COMPONENTS main keypad pcf8574 driver freertos auth_manager)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TEST_PASSWORD_APP)
