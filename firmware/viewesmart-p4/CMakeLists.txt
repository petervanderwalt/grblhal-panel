# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    common_components
    components
    )

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)
project(lvgl_demo_v9)

execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/patches/patch-hosted.py"
    RESULT_VARIABLE PANEL_HOSTED_PATCH_RESULT)
if(NOT PANEL_HOSTED_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Hosted SDIO compatibility patch failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/patch-hosted.py")
