# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "app" "device" "middleware" "util" "third_party")

list(APPEND EXTRA_COMPONENT_DIRS
    "test/test_sdcard_implement"
    "test/test_sdcard_implement/sd_card"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SMART_LOCK_DOOR)
