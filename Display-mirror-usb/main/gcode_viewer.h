#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GCODE_VIEW_PRESET_ISO = 0,
    GCODE_VIEW_PRESET_TOP,
    GCODE_VIEW_PRESET_FRONT,
    GCODE_VIEW_PRESET_LEFT,
    GCODE_VIEW_PRESET_RIGHT,
    GCODE_VIEW_PRESET_BACK,
    GCODE_VIEW_PRESET_BOTTOM,
} gcode_view_preset_t;

typedef void (*gcode_viewer_progress_cb_t)(int progress, void *user_data);

void gcode_viewer_attach(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);
void gcode_viewer_detach(void);
bool gcode_viewer_load_file(const char *path, char *status, size_t status_len);
bool gcode_viewer_parse_memory(const char *data, size_t size, char *status, size_t status_len);
bool gcode_viewer_parser_test(void);
void gcode_viewer_set_progress_callback(gcode_viewer_progress_cb_t cb, void *user_data);
void gcode_viewer_set_preset(gcode_view_preset_t preset);
void gcode_viewer_rotate(float delta_x, float delta_y);
void gcode_viewer_pan(int delta_x, int delta_y);
void gcode_viewer_zoom(float factor);
void gcode_viewer_refresh(void);
bool gcode_viewer_has_model(void);
void gcode_viewer_set_render_count(int count);
int gcode_viewer_get_vertex_count(void);

#ifdef __cplusplus
}
#endif
