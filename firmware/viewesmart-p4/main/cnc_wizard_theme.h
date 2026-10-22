#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIZARD_COLOR_BG              lv_color_hex(0xf0f2f5)
#define WIZARD_COLOR_CARD_BG         lv_color_hex(0xffffff)
#define WIZARD_COLOR_DARK_BG         lv_color_hex(0xf0f2f5)
#define WIZARD_COLOR_DARK_CARD       lv_color_hex(0xffffff)
#define WIZARD_COLOR_DARK_PANEL      lv_color_hex(0xdfe6e9)
#define WIZARD_COLOR_BLUE_ACCENT     lv_color_hex(0x0984e3)
#define WIZARD_COLOR_GREEN_ACCENT    lv_color_hex(0x00b894)
#define WIZARD_COLOR_RED_ACCENT      lv_color_hex(0xff7675)
#define WIZARD_COLOR_RED_DANGER      lv_color_hex(0xff6b6b)
#define WIZARD_COLOR_ORANGE          lv_color_hex(0xe17055)
#define WIZARD_COLOR_PURPLE          lv_color_hex(0x6c5ce7)
#define WIZARD_COLOR_WHITE           lv_color_hex(0xffffff)
#define WIZARD_COLOR_GRAY            lv_color_hex(0x636e72)
#define WIZARD_COLOR_TEXT            lv_color_hex(0x2d3436)
#define WIZARD_COLOR_TEXT_SECONDARY  lv_color_hex(0x636e72)
#define WIZARD_COLOR_LIGHT_GRAY      lv_color_hex(0xdfe6e9)
#define WIZARD_COLOR_DIM_GRAY        lv_color_hex(0x95a5a6)
#define WIZARD_COLOR_INPUT_BG        lv_color_hex(0xf8f9fa)

lv_style_t *wizard_get_container_style(void);
lv_style_t *wizard_get_card_style(void);
lv_style_t *wizard_get_accent_btn_style(void);
lv_style_t *wizard_get_green_btn_style(void);
lv_style_t *wizard_get_panel_style(void);
lv_style_t *wizard_get_scrollbar_style(void);
lv_style_t *wizard_get_danger_btn_style(void);

lv_obj_t *wizard_create_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
lv_obj_t *wizard_create_accent_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb);
lv_obj_t *wizard_create_green_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb);
lv_obj_t *wizard_create_danger_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb);
lv_obj_t *wizard_create_label(lv_obj_t *parent, const char *text, lv_coord_t font_h);
lv_obj_t *wizard_create_title_label(lv_obj_t *parent, const char *text);

#ifdef __cplusplus
}
#endif
