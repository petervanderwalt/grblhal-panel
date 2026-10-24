#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void panel_ui_create(void);
bool panel_ui_setting_field(const char *text);
bool panel_ui_wifi_password(const char *password);
bool panel_ui_actuator_field(unsigned index, const char *text);
bool panel_ui_actuator_json(char *buffer, size_t size);
void panel_ui_settings(char *buffer,size_t size);
#ifdef __cplusplus
}
#endif
