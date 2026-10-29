#include "cnc_wizard_theme.h"

static lv_style_t s_container_style;
static lv_style_t s_card_style;
static lv_style_t s_accent_btn_style;
static lv_style_t s_green_btn_style;
static lv_style_t s_panel_style;
static lv_style_t s_scrollbar_style;
static lv_style_t s_danger_btn_style;
static bool s_styles_inited = false;

static void ensure_styles(void)
{
    if (s_styles_inited) {
        return;
    }
    s_styles_inited = true;

    lv_style_init(&s_container_style);
    lv_style_set_bg_opa(&s_container_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_container_style, WIZARD_COLOR_BG);
    lv_style_set_border_width(&s_container_style, 0);
    lv_style_set_pad_all(&s_container_style, 0);
    lv_style_set_pad_row(&s_container_style, 0);
    lv_style_set_pad_column(&s_container_style, 0);

    lv_style_init(&s_card_style);
    lv_style_set_bg_opa(&s_card_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_card_style, WIZARD_COLOR_CARD_BG);
    lv_style_set_border_width(&s_card_style, 1);
    lv_style_set_border_color(&s_card_style, WIZARD_COLOR_LIGHT_GRAY);
    lv_style_set_radius(&s_card_style, 12);
    lv_style_set_shadow_width(&s_card_style, 0);
    lv_style_set_pad_all(&s_card_style, 16);

    lv_style_init(&s_accent_btn_style);
    lv_style_set_bg_opa(&s_accent_btn_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_accent_btn_style, WIZARD_COLOR_BLUE_ACCENT);
    lv_style_set_radius(&s_accent_btn_style, 8);
    lv_style_set_border_width(&s_accent_btn_style, 0);
    lv_style_set_text_color(&s_accent_btn_style, WIZARD_COLOR_WHITE);
    lv_style_set_text_font(&s_accent_btn_style, &lv_font_montserrat_16);

    lv_style_init(&s_green_btn_style);
    lv_style_set_bg_opa(&s_green_btn_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_green_btn_style, WIZARD_COLOR_GREEN_ACCENT);
    lv_style_set_radius(&s_green_btn_style, 8);
    lv_style_set_border_width(&s_green_btn_style, 0);
    lv_style_set_text_color(&s_green_btn_style, WIZARD_COLOR_WHITE);
    lv_style_set_text_font(&s_green_btn_style, &lv_font_montserrat_16);

    lv_style_init(&s_panel_style);
    lv_style_set_bg_opa(&s_panel_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_style, WIZARD_COLOR_BG);
    lv_style_set_border_width(&s_panel_style, 1);
    lv_style_set_border_color(&s_panel_style, WIZARD_COLOR_LIGHT_GRAY);
    lv_style_set_radius(&s_panel_style, 8);

    lv_style_init(&s_scrollbar_style);

    lv_style_init(&s_danger_btn_style);
    lv_style_set_bg_opa(&s_danger_btn_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_danger_btn_style, WIZARD_COLOR_RED_DANGER);
    lv_style_set_radius(&s_danger_btn_style, 8);
    lv_style_set_border_width(&s_danger_btn_style, 0);
    lv_style_set_text_color(&s_danger_btn_style, WIZARD_COLOR_WHITE);
    lv_style_set_text_font(&s_danger_btn_style, &lv_font_montserrat_16);
}

lv_style_t *wizard_get_container_style(void) { ensure_styles(); return &s_container_style; }
lv_style_t *wizard_get_card_style(void)       { ensure_styles(); return &s_card_style; }
lv_style_t *wizard_get_accent_btn_style(void)  { ensure_styles(); return &s_accent_btn_style; }
lv_style_t *wizard_get_green_btn_style(void)   { ensure_styles(); return &s_green_btn_style; }
lv_style_t *wizard_get_panel_style(void)       { ensure_styles(); return &s_panel_style; }
lv_style_t *wizard_get_scrollbar_style(void)   { ensure_styles(); return &s_scrollbar_style; }
lv_style_t *wizard_get_danger_btn_style(void)  { ensure_styles(); return &s_danger_btn_style; }

lv_obj_t *wizard_create_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, wizard_get_card_style(), 0);
    if (w > 0 && h > 0) {
        lv_obj_set_size(card, w, h);
    } else {
        lv_obj_set_width(card, lv_pct(100));
    }
    return card;
}

lv_obj_t *wizard_create_accent_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_style(btn, wizard_get_accent_btn_style(), 0);
    lv_obj_set_height(btn, 44);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *wizard_create_danger_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_style(btn, wizard_get_danger_btn_style(), 0);
    lv_obj_set_height(btn, 44);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *wizard_create_green_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_style(btn, wizard_get_green_btn_style(), 0);
    lv_obj_set_size(btn, 260, 56);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *wizard_create_label(lv_obj_t *parent, const char *text, lv_coord_t font_h)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, WIZARD_COLOR_TEXT, 0);
    const lv_font_t *font = &lv_font_montserrat_14;
    if (font_h >= 32) font = &lv_font_montserrat_32;
    else if (font_h >= 28) font = &lv_font_montserrat_28;
    else if (font_h >= 22) font = &lv_font_montserrat_22;
    else if (font_h >= 20) font = &lv_font_montserrat_20;
    else if (font_h >= 18) font = &lv_font_montserrat_18;
    else if (font_h >= 16) font = &lv_font_montserrat_16;
    else font = &lv_font_montserrat_14;
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

lv_obj_t *wizard_create_title_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = wizard_create_label(parent, text, 22);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, lv_pct(100));
    return label;
}
