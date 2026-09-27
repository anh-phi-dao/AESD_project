cmake_minimum_required(VERSION 3.16)

# Trỏ ESP-IDF tìm component trong thư mục sd_card
set(EXTRA_COMPONENT_DIRS "sd_card")

# Khai báo sd_card là component chính thay cho main
set(COMPONENTS sd_card esptool_py)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_sdcard_implement)