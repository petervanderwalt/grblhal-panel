#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void panel_ui_create(void);
bool panel_ui_wifi_password(const char *password);
void panel_ui_settings(char *buffer,size_t size);
#ifdef __cplusplus
}
#endif
