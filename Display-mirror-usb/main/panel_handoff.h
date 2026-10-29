#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void panel_handoff_start(void);
void panel_handoff_name(const char *name,char *output,size_t size);
#ifdef __cplusplus
}
#endif
