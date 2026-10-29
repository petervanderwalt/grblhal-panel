cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS components usb_screen)
# Keep the copied pendant sources as reference; only build the USB monitor.
set(COMPONENTS usb_screen)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Display-mirror-usb)
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/patches/patch-lvgl.py"
    RESULT_VARIABLE PATCH_RESULT)
if(NOT PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "LVGL compatibility patch failed")
endif()
