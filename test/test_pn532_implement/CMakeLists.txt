cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "pn532")

set(COMPONENTS pn532 esptool_py)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_pn532_implement)