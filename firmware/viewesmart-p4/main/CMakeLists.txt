idf_component_register(
    SRCS
        main.c
        panel_hardware.cpp
        panel_handoff.cpp
        panel_actuator_web.cpp
        panel_storage.cpp
        panel_fs.cpp
        panel_memory.c
        panel_stream.cpp
        tests/panel_stream_test.cpp
        panel_upload.cpp
        tests/panel_upload_test.cpp
        wifi_bridge.c
        ui_remote.c
        panel_model.cpp
        panel_settings.cpp
        tests/panel_settings_test.cpp
        panel_runtime.cpp
        panel_usb.cpp
        panel_ui.cpp
        assets/wifi_lock.c
        assets/panel_dro_52.c
        assets/panel_action_icons_22.c
        gcode_viewer.cpp
        tests/panel_model_test.cpp
        tests/panel_actuators_test.cpp
        tests/panel_tools_test.cpp
    INCLUDE_DIRS
        .)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=memcpy" "-Wl,--wrap=memmove")
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lv_memcpy" "-Wl,--wrap=lv_memmove")
