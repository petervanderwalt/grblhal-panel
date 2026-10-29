idf_component_register(
    SRCS app_usb.c app_vendor.c usb_extend_screen.c usb_frame.c app_lcd_p4.c app_hid.c app_touch.c
         ../main/panel_memory.c
    INCLUDE_DIRS include tusb
    REQUIRES esp32_p4_ultra esp_driver_jpeg esp_timer usb esp_mm)
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tusb"
)
target_sources(${tusb_lib} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tusb/usb_descriptors.c")
cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=memcpy" "-Wl,--wrap=memmove")
