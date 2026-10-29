#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool panel_debug_test(void);
bool panel_debug_webdav_test(void);
bool panel_memory_test(void);
void panel_debug_demo(bool on);
bool panel_debug_receive(const char *line);
void panel_debug_state(char *buffer, size_t size);
void panel_debug_last(char *buffer, size_t size);
void panel_debug_audit(char *buffer, size_t size, int item);
bool panel_debug_remove_test_file(const char *name);
#ifdef __cplusplus
}
#endif
