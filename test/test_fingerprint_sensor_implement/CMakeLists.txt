cmake_minimum_required(VERSION 3.16)

# Trỏ ESP-IDF tới thư mục ili9341, BSP (SPI, GPIO) và middleware (lvgl)
set(EXTRA_COMPONENT_DIRS
    "ili9341"
    "../../BSP/SPI"
    "../../BSP/GPIO"
    "../../middleware/lvgl"
)

# Khai báo các component chính cần đưa vào bản build
set(COMPONENTS ili9341 SPI lvgl esptool_py)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_ili9341_implement)