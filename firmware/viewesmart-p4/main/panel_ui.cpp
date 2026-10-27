#include "panel_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gcode_viewer.h"
#include "lvgl.h"
#include "nvs.h"
#include "panel_runtime.h"
#include "panel_usb.h"
#include "panel_storage.h"
#include "panel_hardware.h"
#include "bsp/touch.h"
#include "bsp/display.h"
#include "esp_lcd_touch.h"
#include "panel_actuators.h"
#include "panel_macros.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <new>
#include "wifi_bridge.h"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
LV_IMAGE_DECLARE(panel_wifi_lock);
LV_FONT_DECLARE(panel_dro_52);
LV_FONT_DECLARE(panel_action_icons_22);
}

namespace {
using namespace panel;
constexpr uint32_t BG = 0xD9E2ED, CARD = 0xFFFFFF, EDGE = 0x30363B, TEXT = 0x30363B, MUTED = 0x566273,
                   PRIMARY = 0x176FB5, RED = 0xD3343B, GREEN = 0x238636, AMBER = 0xF2B94F, WHITE = 0xFFFFFF,
                   LINE = 0xE2E8F1, DISABLED = 0xE8EDF5, TRACK = 0x7F8FA3;
constexpr uint32_t AXIS_COLORS[] = {RED, 0x2D8530, 0x176FB5};
constexpr uint32_t XY_COLOR = 0x247C87;
struct Preferences {
    uint32_t magic = 0x50414E31;
    ProbeSettings probe;
    float tx = 37, rx = 38, baud = 115200, step = 1, jog_feed = 600;
};
static Preferences prefs;
static ActuatorPreferences actuator_prefs;
static bool actuator_mode = false;
static bool usb_connection = false;
static lv_obj_t *usb_connection_status, *controller_connect, *controller_demo;
static bool a_rotary = false;
static bool actuator_config_open = false;
static State state;
static wifi_bridge_state_t wifi;
static lv_obj_t *wifi_row_status, *wifi_status, *wifi_list, *wifi_spinner, *wifi_scan_btn,
    *wifi_disconnect_btn, *wifi_password;
static unsigned wifi_revision;
static char wifi_ssid[33];
static uint8_t wifi_auth;
static bool wifi_attempt;
static lv_obj_t *wifi_password_error;
static void show_wifi(lv_event_t * = nullptr);
enum { JogPage, FilesPage, SettingsPage, MacrosPage, JobPage, CompletePage, DiagnosticsPage };
static constexpr int tab_pages[] = {JogPage, FilesPage, MacrosPage, DiagnosticsPage, SettingsPage};
static int tab_for_page(int value) {
    for (int i = 0; i < 5; ++i) if (tab_pages[i] == value) return i;
    return -1;
}
static int page = JogPage;
static bool inches = false, probe_return = false;
static bool single_probe = true;
static unsigned probe_surface_choice = 0;
static const int surface_axes[] = {2, 0, 0, 1, 1};
static const int surface_directions[] = {-1, -1, 1, -1, 1};
static uint8_t step_index = 1;
static lv_obj_t *jog_tools, *wcs_dropdown, *feed_slider, *spindle_slider, *feed_text, *spindle_text;
static int fault_seen = 0;
static uint32_t override_sent = 0;
static void show_probe(lv_event_t * = nullptr);
static lv_obj_t *left_surface, *right_surface, *tabview, *page_layer;
static lv_obj_t *wifi_header, *wifi_header_name, *wifi_header_ip, *wifi_icon;
static lv_obj_t *root, *body, *brand_label, *state_label, *axis_work[3], *axis_machine[3],
    *axis_card[3], *nav[5];
static lv_obj_t *accessories_btn, *page_title;
static bool accessories_open = false, spindles_open = false, macro_editor_open = false;
static lv_obj_t *macro_fields[2];
static lv_obj_t *home_btn, *stop_btn, *job_controls, *job_pause, *probe_btn;
static lv_obj_t *speed_choices, *mode_choices, *step_dropdown;
static lv_obj_t *axis_zero[3], *axis_home[3];
static lv_obj_t *axis_name[3], *machine_heading, *work_heading;
static uint8_t jog_speed = 0;
static lv_obj_t *dro_actions, *goto_buttons[2], *zero_all_btn;
static bool preview_pending = false, preview_show = false;
static bool local_preview_show=false,server_open=false;
static lv_obj_t *file_prepare_status=nullptr,*file_prepare_spinner=nullptr,*file_prepare_name=nullptr;
static lv_obj_t *cached_image=nullptr,*cached_status=nullptr,*server_field=nullptr;
static bool preview_update_open = false, preview_update_cancelled = false;
static lv_obj_t *preview_update_name, *preview_update_status, *preview_update_bar, *preview_update_overall, *preview_update_button, *preview_update_spinner;
static void preview_update_refresh();
static void cached_refresh();
static void show_preview_server(lv_event_t *);
static void concept_form_dialog(lv_obj_t *card, lv_obj_t *keyboard, lv_obj_t *const *fields, unsigned count);
static void show_probe_offsets(lv_event_t *);
static void build_cached_preview();
static std::atomic<int> preview_parsing{0};
static const char *preview_data;
static size_t preview_size;
static char preview_path[160], preview_message[192];
static lv_obj_t *file_box, *file_title, *progress, *progress_text, *time_text, *start_btn, *refresh_btn,
    *probe_start, *probe_status, *contact_btn;
static lv_obj_t *modal = nullptr, *number = nullptr, *number_error = nullptr;
static bool modal_plain = false, modal_inset_pending = false;
static void finish_modal(lv_obj_t *);
static void form_control_style(lv_obj_t *);
static bool form_object(lv_obj_t *o) {
    if (page == JobPage && o == stop_btn) return true;
    for (; o; o = lv_obj_get_parent(o)) {
        if (modal && !modal_plain && o == modal) return true;
        if ((page == JobPage || (page == JogPage && actuator_mode)) && (o == body || o == job_controls)) return true;
    }
    return false;
}
static char selected[160] = "";
static uint32_t revision = 0;
static unsigned controller_file_page=0;
static uint8_t screen_brightness = 100, saved_brightness = 100;
static lv_obj_t *brightness_value = nullptr;
static lv_obj_t *file_run_buttons[4]={};
static bool file_run_usable[4]={};
static lv_obj_t *file_previous=nullptr,*file_next=nullptr,*file_page_label=nullptr;
static bool continuous = false, jog_held = false;
static uint32_t jog_pressed_at, jog_hold_ms;
static uint32_t jog_control_key = UINT32_MAX, jog_control_updates;
static lv_obj_t *jog_buttons[10];
static const char *const jog_directions[] = {"-+0", "0+0", "++0", "-00", "+00", "--0", "0-0", "+-0", "00+", "00-"};
static int jog_count;
static char home_axis;
struct Edit {
    float *value;
    float min, max;
    const char *title;
    bool integer = false;
    bool converted = false;
};
static Edit editing;
static char edit_initial[24];
static bool probe_offsets_open = false;
static lv_obj_t *probe_offset_fields[5] = {}, *probe_offsets_keyboard = nullptr, *probe_offsets_error = nullptr;
static char probe_offsets_initial[5][24];
static bool (*confirmation)() = nullptr;
static lv_obj_t *confirmation_message, *confirmation_action;
static bool fault_confirmation=false;
static int fault_confirmation_id=0;
static void build_page();
static void release_jog();
static void refresh(lv_timer_t *);
static void machine_closed();
static void machine_refresh();
static void show_machine_settings(lv_event_t *);
static void parent_page_content(lv_obj_t *parent) {
    lv_obj_t *objects[] = {left_surface, right_surface, axis_card[0], axis_card[1], axis_card[2],
                           machine_heading, work_heading, dro_actions, jog_tools, job_controls, body, stop_btn, page_title};
    for (auto obj : objects)
        if (obj && lv_obj_get_parent(obj) != parent)
            lv_obj_set_parent(obj, parent);
    if (parent == root && wifi_header && state_label) {
        lv_obj_move_foreground(wifi_header);
        lv_obj_move_foreground(state_label);
    }
}
static void show_page(int target) {
    page = target;
    build_page();
}
static uint32_t now() { return lv_tick_get(); }
static void layout_stop() {
    if (!stop_btn)
        return;
    if (page == JobPage && !modal)
        lv_obj_remove_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);
}
static void save() {
    nvs_handle_t h;
    if (nvs_open("panel", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "prefs", &prefs, sizeof prefs);
        nvs_set_u8(h, "jogspeed", jog_speed);
        nvs_set_u8(h, "inches", inches);
        nvs_set_u8(h, "stepidx", step_index);
        nvs_set_u8(h, "application", actuator_mode);
        nvs_set_u8(h, "usb", usb_connection);
        nvs_set_u8(h, "arotary", a_rotary);
        nvs_set_blob(h, "actuators", &actuator_prefs, sizeof actuator_prefs);
        nvs_commit(h);
        nvs_close(h);
    }
}
static void load() {
    nvs_handle_t h;
    if (nvs_open("panel", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof prefs;
        Preferences p;
        if (nvs_get_blob(h, "prefs", &p, &n) == ESP_OK && n == sizeof p && p.magic == prefs.magic) {
            float v[] = {p.probe.x, p.probe.y, p.probe.z, p.probe.feed, p.probe.inset,   p.tx,
                         p.rx,      p.baud,    p.step,    p.jog_feed,   p.probe.diameter};
            bool valid = true;
            for (float f : v)
                if (!isfinite(f) || f < 0)
                    valid = false;
            if (valid && p.probe.x <= 50 && p.probe.y <= 50 && p.probe.z <= 50 && p.probe.feed <= 500 &&
                p.probe.inset <= 100 && p.probe.diameter <= 50 && p.tx <= 54 && p.rx <= 54 &&
                p.baud >= 9600 && p.baud <= 921600 && p.step > 0 && p.step <= 100 && p.jog_feed > 0 &&
                p.jog_feed <= 10000 && p.probe.corner >= 0 && p.probe.corner <= 3)
                prefs = p;
        }
        uint8_t speed;
        if (nvs_get_u8(h, "jogspeed", &speed) == ESP_OK && speed <= 2)
            jog_speed = speed;
        uint8_t light = 100;
        if (nvs_get_u8(h, "backlight", &light) == ESP_OK && light >= 10 && light <= 100)
            screen_brightness = saved_brightness = light;
        uint8_t unit, step;
        if (nvs_get_u8(h, "inches", &unit) == ESP_OK)
            inches = unit == 1;
        if (nvs_get_u8(h, "stepidx", &step) == ESP_OK && step < 4)
            step_index = step;
        uint8_t usb = 0;
        nvs_get_u8(h, "usb", &usb); usb_connection = usb == 1;
        uint8_t application = 0;
        nvs_get_u8(h, "application", &application);
        actuator_mode = application == 1;
        uint8_t rotary = 0;
        nvs_get_u8(h, "arotary", &rotary);
        a_rotary = rotary == 1;
        ActuatorPreferences actuators;
        n = sizeof actuators;
        if (nvs_get_blob(h, "actuators", &actuators, &n) == ESP_OK &&
            n == sizeof actuators && actuators.valid())
            actuator_prefs = actuators;
        nvs_close(h);
    }
    prefs.tx=ControllerTxPin; prefs.rx=ControllerRxPin;
}
static const lv_font_t *font(int size) {
    switch (size) {
    case 12:
        return &lv_font_montserrat_12;
    case 14:
        return &lv_font_montserrat_14;
    case 18:
        return &lv_font_montserrat_18;
    case 20:
        return &lv_font_montserrat_20;
    case 22:
        return &lv_font_montserrat_22;
    case 28:
        return &lv_font_montserrat_28;
    case 32:
        return &lv_font_montserrat_32;
    default:
        return &lv_font_montserrat_16;
    }
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color = CARD) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(LINE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *surface(lv_obj_t *p, int x, int y, int w, int h) {
    lv_obj_t *o = box(p, x, y, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 14, 0);
    lv_obj_set_style_shadow_ofs_y(o, 4, 0);
    lv_obj_set_style_shadow_color(o, lv_color_hex(EDGE), 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_10, 0);
    return o;
}
static uint32_t button_text(uint32_t color) {
    return color == AMBER || color == CARD || color == BG ? TEXT : WHITE;
}
static lv_obj_t *label(lv_obj_t *p, const char *s, int x, int y, int w, int size = 16,
                       uint32_t color = TEXT) {
    lv_obj_t *o = lv_label_create(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_text(o, s);
    lv_obj_set_style_text_font(o, font(size), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}
static lv_obj_t *button(lv_obj_t *p, const char *s, int x, int y, int w, int h, lv_event_cb_t cb,
                        uintptr_t data = 0, uint32_t color = EDGE, bool cancel = false) {
    if (cancel) color = CARD;
    if (color == EDGE)
        color = BG;
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_grad_color(b, lv_color_lighten(lv_color_hex(color), 12), 0);
    lv_obj_set_style_bg_grad_dir(b, color == PRIMARY ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_shadow_ofs_y(b, 3, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_opa(b, LV_OPA_TRANSP, LV_STATE_DISABLED);
    lv_obj_set_style_pad_all(b, 3, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font(16), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(cancel ? RED : button_text(color)), 0);
    if (cancel) {
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(RED), 0);
    }
    lv_obj_set_width(l, w - 10);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)data);
    return b;
}
static void navigation_back(lv_obj_t *parent, lv_event_cb_t callback) {
    auto back = button(parent, LV_SYMBOL_LEFT, 12, 12, 56, 48, callback);
    lv_obj_set_style_text_font(lv_obj_get_child(back, 0), font(22), 0);
}
static void brightness_changed(lv_event_t *e) {
    screen_brightness = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    bsp_display_brightness_set(screen_brightness);
    char text[16]; snprintf(text, sizeof text, "%u%%", screen_brightness);
    if (brightness_value) lv_label_set_text(brightness_value, text);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED && screen_brightness != saved_brightness) {
        nvs_handle_t h;
        if (nvs_open("panel", NVS_READWRITE, &h) == ESP_OK) {
            if (nvs_set_u8(h, "backlight", screen_brightness) == ESP_OK && nvs_commit(h) == ESP_OK)
                saved_brightness = screen_brightness;
            nvs_close(h);
        }
    }
}
static void enable(lv_obj_t *b, bool on, bool brief_busy = false) {
    if (!b)
        return;
    if (form_object(b)) {
        bool changed = lv_obj_has_state(b,LV_STATE_DISABLED) == on;
        if (changed) {
            if (on) lv_obj_remove_state(b,LV_STATE_DISABLED); else lv_obj_add_state(b,LV_STATE_DISABLED);
            form_control_style(b);
        }
        return;
    }
    if (on)
        lv_obj_remove_state(b, LV_STATE_DISABLED);
    else
        lv_obj_add_state(b, LV_STATE_DISABLED);
    lv_obj_set_style_opa(b, LV_OPA_COVER, 0);
    lv_style_value_t base;
    lv_color_t disabled_bg = lv_color_hex(DISABLED);
    if (brief_busy && lv_obj_get_local_style_prop(b, LV_STYLE_BG_COLOR, &base, 0) == LV_STYLE_RES_FOUND)
        disabled_bg = base.color;
    lv_obj_set_style_bg_color(b, disabled_bg, LV_STATE_DISABLED);
    lv_grad_dir_t gradient = LV_GRAD_DIR_NONE;
    if (brief_busy && lv_obj_get_local_style_prop(b, LV_STYLE_BG_GRAD_DIR, &base, 0) == LV_STYLE_RES_FOUND)
        gradient = (lv_grad_dir_t)base.num;
    lv_obj_set_style_bg_grad_dir(b, gradient, LV_STATE_DISABLED);
    if (brief_busy && lv_obj_get_local_style_prop(b, LV_STYLE_BG_GRAD_COLOR, &base, 0) == LV_STYLE_RES_FOUND)
        lv_obj_set_style_bg_grad_color(b, base.color, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, lv_color_hex(brief_busy ? TEXT : MUTED), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, lv_color_hex(brief_busy ? TEXT : MUTED), LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_color_filter_opa(b, LV_OPA_TRANSP, LV_STATE_DISABLED);
    if (lv_obj_check_type(b, &lv_button_class) && lv_obj_get_child_count(b)) {
        lv_obj_t *text = lv_obj_get_child(b, 0);
        if (lv_obj_check_type(text, &lv_label_class)) {
            uint32_t color = lv_color_to_u32(lv_obj_get_style_bg_color(b, 0)) & 0xFFFFFF;
            lv_obj_set_style_text_color(text, lv_color_hex(on || brief_busy ? button_text(color) : MUTED), 0);
        } else if (lv_obj_check_type(text, &lv_line_class))
            lv_obj_set_style_line_color(text, lv_color_hex(on || brief_busy ? WHITE : MUTED), 0);
    }
}
static void soften(lv_obj_t *b, uint32_t accent = LINE) {
    lv_obj_set_style_bg_color(b, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_mix(lv_color_hex(accent), lv_color_hex(CARD), 72), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(TEXT), 0);
}
static lv_obj_t *choices(lv_obj_t *parent, const char *const *map, int selected, int x, int y, int w, int h,
                    lv_event_cb_t callback) {
    lv_obj_t *m = lv_buttonmatrix_create(parent);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, w, h);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    // Checked state updates on release; callbacks may refresh or rebuild the matrix.
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_buttonmatrix_set_button_ctrl(m, selected, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_column(m, 8, 0);
    lv_obj_set_style_bg_color(m, lv_color_hex(BG), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(m, LV_GRAD_DIR_NONE, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 14, LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, font(16), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(TEXT), LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(m, lv_color_hex(LINE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(CARD), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, lv_color_hex(PRIMARY), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(m, lv_color_hex(PRIMARY), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(m, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    const lv_style_selector_t disabled[] = {LV_PART_ITEMS | LV_STATE_DISABLED,
                                            LV_PART_ITEMS | LV_STATE_DISABLED | LV_STATE_CHECKED};
    for (auto selector : disabled) {
        lv_obj_set_style_bg_color(m, lv_color_hex(DISABLED), selector);
        lv_obj_set_style_text_color(m, lv_color_hex(MUTED), selector);
        lv_obj_set_style_border_color(m, lv_color_hex(LINE), selector);
        lv_obj_set_style_border_width(m, 0, selector);
    }
    if (callback)
        lv_obj_add_event_cb(m, callback, LV_EVENT_VALUE_CHANGED, nullptr);
    return m;
}
static void enable_choices(lv_obj_t *m, bool on, bool brief_busy = false) {
    if (!m)
        return;
    bool unchanged = lv_obj_has_state(m, LV_STATE_DISABLED) == !on;
    enable(m, on, brief_busy);
    for (int checked = 0; checked < 2; checked++) {
        lv_style_selector_t selector = LV_PART_ITEMS | LV_STATE_DISABLED | (checked ? LV_STATE_CHECKED : 0);
        lv_obj_set_style_bg_color(m, lv_color_hex(brief_busy ? (checked ? CARD : BG) : DISABLED), selector);
        lv_obj_set_style_text_color(m, lv_color_hex(brief_busy ? (checked ? PRIMARY : TEXT) : MUTED), selector);
        lv_obj_set_style_border_color(m, lv_color_hex(brief_busy && checked ? PRIMARY : LINE), selector);
        lv_obj_set_style_border_width(m, brief_busy ? (checked ? 2 : 1) : 0, selector);
        lv_obj_set_style_color_filter_opa(m, LV_OPA_TRANSP, selector);
    }
    if (unchanged)
        return;
    if (on)
        lv_buttonmatrix_clear_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_DISABLED);
    else
        lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_DISABLED);
}
static bool ready() {
    return state.online && state.status && state.identified && !state.busy && !state.recovering && !state.alarm && !state.error &&
           !state.sd_active && !strcmp(state.mode, "Idle");
}
static void notify(const char *s);
static void result(bool ok) {
    if (!ok)
        notify("Action unavailable: check connection, machine state and setup.");
}
static void close_modal() {
    if (preview_update_open) storage_update_cancel();
    preview_update_open = false;
    machine_closed();
    probe_offsets_open = false;
    for (auto &field : probe_offset_fields) field = nullptr;
    probe_offsets_keyboard = probe_offsets_error = nullptr;
    accessories_open = spindles_open = macro_editor_open = server_open = false;
    macro_fields[0] = macro_fields[1] = nullptr;
    actuator_config_open = false;
    if (modal) lv_obj_delete(modal);
    modal = nullptr;
    number = nullptr;
    number_error = nullptr;
    confirmation = nullptr;
    confirmation_message=confirmation_action=nullptr; fault_confirmation=false;
    wifi_password_error = nullptr;
    wifi_status = wifi_list = wifi_spinner = wifi_scan_btn = wifi_disconnect_btn = wifi_password = nullptr;
    probe_start = probe_status = contact_btn = nullptr;
    layout_stop();
}
static lv_obj_t *open_modal(const char *title, int width = 1024, int height = 600, uint32_t accent = PRIMARY, bool plain = false) {
    close_modal();
    modal_plain = plain;
    modal_inset_pending = !plain && (width != 1024 || height != 600);
    modal = box(lv_layer_top(), 0, 0, 1024, 600, 0x34373C);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    auto card = box(modal, 0, 0, 1024, 600, plain ? CARD : 0x34373C);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    auto panel = box(card, plain ? 0 : 16, plain ? 0 : 76, plain ? width : 992, plain ? 64 : 508, plain ? accent : 0x080B0D);
    lv_obj_set_style_radius(panel, plain ? 0 : 28, 0);
    lv_obj_set_style_border_width(panel, !plain && accent != PRIMARY ? 2 : 0, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(accent), 0);
    label(card, title, plain ? 24 : 40, plain ? 22 : 20, plain ? width - 48 : 864, plain ? 22 : 28,
          plain ? button_text(accent) : accent == RED ? 0xFF8379 : 0xF2F5F4);
    layout_stop();
    return card;
}

static void confirmed(lv_event_t *) {
    auto fn = confirmation;
    close_modal();
    if (fn)
        result(fn());
    if (probe_return) {
        probe_return = false;
        show_probe();
    }
}
static void cancelled(lv_event_t *) {
    close_modal();
    if (probe_return) {
        probe_return = false;
        show_probe();
    }
}
static void modal_close_button(lv_obj_t *card, int width = 1024, lv_event_cb_t callback = cancelled) {
    auto close = button(card, LV_SYMBOL_CLOSE, modal_plain ? width - 88 : 944, 8, 64, 48, callback);
    lv_obj_set_style_bg_opa(close, modal_plain ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(close, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(close, 0), lv_color_hex(0xF2F5F4), 0);
}

static void notify(const char *s) {
    auto c = open_modal("Notice");
    auto text = label(c, s, 80, 164, 864, 22);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    button(c, "OK", 660, 488, 324, 64, cancelled, 0, GREEN);
    finish_modal(c);
}
static void confirm(const char *title, const char *text, bool (*fn)(), uint32_t accent = AMBER,
                    const char *action = "Confirm", uint32_t action_color = GREEN, uint32_t cancel_color = RED) {
    auto c = open_modal(title, 1024, 600, accent);
    auto message = label(c, text, 80, 150, 864, 22, MUTED);
    confirmation_message=message;
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    confirmation = fn;
    auto cancel = button(c, "Cancel", 40, 488, 448, 64, cancelled, 0, cancel_color);
    if (cancel_color != RED) lv_obj_add_state(cancel, LV_STATE_USER_2);
    confirmation_action=button(c, action, 536, 488, 448, 64, confirmed, 0, action_color);
    finish_modal(c);
}

static void key(lv_event_t *e) {
    const char *k = (const char *)lv_event_get_user_data(e);
    if (!strcmp(k, "Clear"))
        lv_textarea_set_text(number, "");
    else if (!strcmp(k, "Back"))
        lv_textarea_delete_char(number);
    else
        lv_textarea_add_text(number, k);
}
static void apply_number(lv_event_t *) {
    char *end;
    const char *s = lv_textarea_get_text(number);
    float f = strtof(s, &end);
    if (editing.converted)
        f *= display_scale(inches);
    if (!strcmp(s, edit_initial))
        f = *editing.value;
    if (!*s || *end || !isfinite(f) || f < editing.min || f > editing.max ||
        (editing.integer && floorf(f) != f)) {
        lv_label_set_text(number_error, "Outside range / check the number");
        return;
    }
    *editing.value = f;
    save();
    close_modal();
    build_page();
    if (probe_return) {
        probe_return = false;
        show_probe();
    }
}
static void edit(Edit e) {
    e.converted = !e.integer;
    editing = e;
    lv_obj_t *c = open_modal(e.title);
    lv_obj_set_width(lv_obj_get_child(c,0),600);
    auto keypad_card=box(c,632,76,376,508);
    lv_obj_set_style_radius(keypad_card,28,0);
    lv_obj_set_style_border_width(keypad_card,0,0);
    modal_close_button(c);
    label(c,e.integer?"Baud rate":"Tool diameter",40,108,200,22,TEXT);
    label(c,e.integer?"baud":(inches?"in":"mm"),40,136,200,14,MUTED);
    number = lv_textarea_create(c);
    lv_obj_set_pos(number,264,100); lv_obj_set_size(number,328,64);
    lv_textarea_set_one_line(number,true); lv_obj_set_height(number,64);
    lv_textarea_set_max_length(number,10);
    lv_textarea_set_accepted_chars(number,e.integer?"0123456789":"0123456789.");
    char value[24];
    snprintf(value,sizeof value,e.integer?"%.0f":"%.3f",*e.value/(e.converted?display_scale(inches):1));
    snprintf(edit_initial,sizeof edit_initial,"%s",value);
    lv_textarea_set_text(number,value);
    lv_obj_set_style_text_font(number,font(22),0);
    lv_obj_set_style_text_align(number,LV_TEXT_ALIGN_RIGHT,0);
    auto clear=button(c,"Clear",424,180,168,48,nullptr);
    lv_obj_add_event_cb(clear,key,LV_EVENT_CLICKED,(void *)"Clear");
    char limits[80];
    snprintf(limits,sizeof limits,"Range %.3f to %.3f",e.min/(e.converted?display_scale(inches):1),e.max/(e.converted?display_scale(inches):1));
    label(c,limits,40,192,368,16,MUTED);
    number_error=label(c,"",40,244,552,20,RED);
    button(c,"Cancel",40,496,268,64,cancelled,0,RED);
    button(c,"Apply",324,496,268,64,apply_number,0,GREEN);
    auto keyboard=lv_keyboard_create(c);
    lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,656,100); lv_obj_set_size(keyboard,328,460);
    static const char *const decimal_keys[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n",".","0",LV_SYMBOL_BACKSPACE,""};
    static const char *const integer_keys[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n","00","0",LV_SYMBOL_BACKSPACE,""};
    static constexpr auto digit=(lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    static const lv_buttonmatrix_ctrl_t controls[]={digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,(lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_CHECKED)};
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_NUMBER,e.integer?integer_keys:decimal_keys,controls);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(keyboard,number);
    finish_modal(c);
    lv_obj_set_style_text_font(keyboard,font(28),LV_PART_ITEMS);
    lv_obj_add_state(number,LV_STATE_FOCUSED);
}
static void edit_value(lv_event_t *e) {
    if (state.probing) {
        notify("Wait for the probe sequence to finish before editing geometry.");
        return;
    }
    probe_return = probe_status != nullptr;
    switch ((uintptr_t)lv_event_get_user_data(e)) {
    case 2:
        edit({&prefs.probe.diameter, .1f, 50, inches ? "Tool diameter (in)" : "Tool diameter (mm)"});
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7: {
        unsigned index=(uintptr_t)lv_event_get_user_data(e)-3;
        show_probe_offsets(nullptr);
        if (probe_offsets_keyboard) lv_obj_send_event(probe_offset_fields[index],LV_EVENT_FOCUSED,nullptr);
        break;
    }
    case 10:
        edit({&prefs.baud, 9600, 921600, "UART baud rate", true});
        break;
    }
}
static void value_button(lv_obj_t *p, const char *title, float value, int x, int y, int w, int id) {
    char text[80];
    snprintf(text, sizeof text, id >= 8 ? "%s\n%.0f" : "%s\n%.3f", title,
             value / (id < 8 ? display_scale(inches) : 1));
    button(p, text, x, y, w, 66, edit_value, id);
}
static bool stop() {
    release_jog();
    bool ok = runtime_action(Action::Stop);
    if (ok)
        show_page(JogPage);
    return ok;
}
static bool home() {
    char axis[] = {home_axis, 0};
    return runtime_action(Action::Home, home_axis ? axis : nullptr);
}
static bool run() {
    bool ok = runtime_action(Action::Run, selected);
    if (ok)
        show_page(JobPage);
    return ok;
}
static bool probe() {
    return single_probe ? runtime_probe(prefs.probe, surface_axes[probe_surface_choice], surface_directions[probe_surface_choice])
                        : runtime_probe(prefs.probe);
}
static void home_click(lv_event_t *e) {
    home_axis = (uintptr_t)lv_event_get_user_data(e);
    char title[40];
    snprintf(title, sizeof title, home_axis ? "Home %c axis?" : "Home all axes?", home_axis);
    confirm(title, "Clear the machine travel before homing.", home, AMBER, "Home");
}
static void stop_click(lv_event_t *) {
    probe_return = false;
    confirm("Stop job?", "Are you sure you want to stop this job?\nThe controller will reset and the job cannot resume.", stop, RED, "Stop", RED);
}
static void pause_click(lv_event_t *) { result(runtime_action(Action::Pause)); }
static void resume_click(lv_event_t *) { result(runtime_action(Action::Resume)); }
static void job_pause_click(lv_event_t *) {
    runtime_snapshot(state);
    if (!strncmp(state.mode, "Hold", 4))
        resume_click(nullptr);
    else
        pause_click(nullptr);
}
static void release_jog() {
    if (jog_held) {
        jog_held = false;
        result(runtime_action(Action::JogCancel));
        jog_hold_ms = now() - jog_pressed_at;
    }
}
static void jog_event(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        if (jog_held)
            return;
        bool ok = runtime_action(Action::Jog, jog_directions[(uintptr_t)lv_event_get_user_data(e)],
                                 continuous ? 10000 : jog_step_mm(inches, step_index), jog_speed);
        jog_held = ok && continuous;
        if (jog_held) jog_pressed_at = now();
        if (!ok) {
            runtime_snapshot(state);
            if (state.online && !state.alarm && !state.error &&
                (state.busy || !strncmp(state.mode, "Jog", 3))) return;
        }
        result(ok);
    } else if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
        release_jog();
    }
}
static void jog_mode(lv_event_t *e) {
    release_jog();
    continuous = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1;
    refresh(nullptr);
}
static void speed_click(lv_event_t *e) {
    release_jog();
    jog_speed = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e));
    save();
    refresh(nullptr);
}
static void list_click(lv_event_t *) { result(runtime_action(Action::List)); }
static void start_click(lv_event_t *) {
    char text[240];
    snprintf(text, sizeof text, "Run %s from %s SD?\nConfirm tool, work zero and clear travel.",
             selected, storage_source()?"display":"controller");
    confirm("Start SD job?", text, run, AMBER, "Run");
}
static void parse_preview(void *) {
    bool ok = gcode_viewer_parse_memory(preview_data, preview_size, preview_message, sizeof preview_message);
    runtime_preview_release();
    preview_parsing.store(ok ? 2 : 3);
    vTaskDelete(nullptr);
}
static void preview_click(lv_event_t *) {
    if(storage_source()) {
        snprintf(preview_path,sizeof preview_path,"%s",selected);
        local_preview_show=true;storage_lookup_preview(selected);build_page();return;
    }
    if (preview_parsing.load() || preview_pending)
        return;
    if (runtime_action(Action::Preview, selected)) {
        snprintf(preview_path, sizeof preview_path, "%s", selected);
        preview_pending = true;
        preview_show = false;
    } else {
        runtime_snapshot(state);
        notify(state.message);
    }
}
static void preview_back(lv_event_t *) {
    preview_show = false;
    build_page();
}
static void view_action(lv_event_t *e) {
    switch ((uintptr_t)lv_event_get_user_data(e)) {
    case 0:
        gcode_viewer_set_preset(GCODE_VIEW_PRESET_TOP);
        break;
    case 1:
        gcode_viewer_set_preset(GCODE_VIEW_PRESET_ISO);
        break;
    }
}
static void file_action(lv_event_t *e) {
    unsigned action = (uintptr_t)lv_event_get_user_data(e);
    int index = action / 2;
    runtime_snapshot(state);
    if(state.listing)return;
    if(state.file_revision!=revision){build_page();return;}
    if (((action&1)?!ready():!storage_source()) || index >= state.file_count || state.files[index].directory || state.files[index].unusable)
        return;
    snprintf(selected, sizeof selected, "%s", state.files[index].path);
    if (action & 1)
        start_click(nullptr);
    else
        preview_click(nullptr);
}
static void file_page_changed(lv_event_t *e) {
    int delta=(intptr_t)lv_event_get_user_data(e);
    if(storage_source()) storage_page(delta);
    else controller_file_page=std::clamp<int>((int)controller_file_page+delta,0,state.file_count?(state.file_count-1)/4:0);
    runtime_snapshot(state);build_page();
}
static void concept_files_style_rows();
static void files() {
    if (!file_box)
        return;
    memset(file_run_buttons,0,sizeof file_run_buttons);
    lv_obj_clean(file_box);
    revision = state.file_revision;
    if (!state.file_count) {
        char message[160];storage_status(message,sizeof message);
        label(file_box, storage_source()?message:state.listing ? "Reading SD card..." : "No files", 18, 24, 900, 18, MUTED);
        concept_files_style_rows();
        return;
    }
    if(!storage_source())controller_file_page=std::min(controller_file_page,state.file_count?(unsigned)(state.file_count-1)/4:0);
    unsigned offset=storage_source()?state.file_offset:controller_file_page*4;
    unsigned total=storage_source()?state.file_total:state.file_count;
    if(file_page_label){char page_text[64];snprintf(page_text,sizeof page_text,"%u-%u / %u",total?offset+1:0,std::min(offset+4,total),total);lv_label_set_text(file_page_label,page_text);enable(file_previous,offset>0);enable(file_next,offset+4<total);}
    int first=storage_source()?0:controller_file_page*4;
    for (int i = first; i < std::min(first+4,state.file_count); i++) {
        lv_obj_t *row = box(file_box, 0, (i-first) * 72, 990, 66);
        lv_obj_t *name = label(row, state.files[i].path, 16, 22, 516, 18);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        char size[40];
        if(state.files[i].size<1024)snprintf(size,sizeof size,"%lu B",(unsigned long)state.files[i].size);
        else snprintf(size, sizeof size, state.files[i].size>=1024*1024?"%.1f MB":"%.1f KB",state.files[i].size/(state.files[i].size>=1024*1024?1048576.f:1024.f));
        label(row, state.files[i].directory ? "Folder" : size, 552, 24, 130, 16, MUTED);
        bool available = !state.files[i].directory && !state.files[i].unusable;
        file_run_usable[i-first]=available;
        if(storage_source()) enable(button(row, "Preview", 696, 8, 132, 50, file_action, i * 2), available);
        file_run_buttons[i-first]=button(row,"Run",840,8,132,50,file_action,i*2+1,GREEN);
        enable(file_run_buttons[i-first],available&&ready());
    }
    concept_files_style_rows();
}
static void contact(lv_event_t *) { result(runtime_action(Action::Contact)); }
static void corner(lv_event_t *e) {
    if (state.probing)
        return;
    prefs.probe.corner = (uintptr_t)lv_event_get_user_data(e);
    save();
    show_probe();
}
static void probe_click(lv_event_t *) {
    probe_return = true;
    if (single_probe) {
        int axis = surface_axes[probe_surface_choice];
        char title[64], text[192];
        snprintf(title, sizeof title, "Probe %c%c surface?", "XYZ"[axis], surface_directions[probe_surface_choice] < 0 ? '-' : '+');
        snprintf(text, sizeof text, "Spindle OFF. Place the plate on the selected surface.\nOnly %c work zero will change.\nMaximum travel %.3f %s; retract %.3f %s.", "XYZ"[axis],
                 25 / display_scale(inches), inches ? "in" : "mm", 2 / display_scale(inches), inches ? "in" : "mm");
        confirm(title, text, probe, AMBER, "Probe");
    } else confirm("Start corner probe?", "Start above the plate hole, spindle OFF.\nThe active WCS XYZ offsets will change.", probe, AMBER, "Probe");
}
static int zero_axes;
static bool zero_confirm() { return runtime_action(Action::Zero, nullptr, zero_axes); }
static void zero_click(lv_event_t *e) {
    zero_axes = (uintptr_t)lv_event_get_user_data(e);
    confirm("Set work zero?",
            "Set the selected axes to zero in the active WCS.\nMachine coordinates are unchanged.",
            zero_confirm, PRIMARY, "Set zero", GREEN, CARD);
}
static int goto_axes;
static bool goto_confirm() { return runtime_action(Action::GotoZero, nullptr, goto_axes); }
static void goto_click(lv_event_t *e) {
    goto_axes = (uintptr_t)lv_event_get_user_data(e);
    confirm(goto_axes == 3 ? "Go to XY work zero?" : "Go to Z work zero?",
            goto_axes == 3 ? "Rapid via MACHINE Z -5 mm, then X0, then Y0.\nReturn Z to its current machine "
                             "position.\nClear the entire travel first."
                           : "Rapid to Z0 in the active work coordinates.\nClear Z travel first.",
            goto_confirm);
}
static void jog_arrow(lv_obj_t *button, int direction) {
    static lv_point_precise_t points[8][5];
    static bool initialized = false;
    if (!initialized) {
        const int directions[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        for (int i = 0; i < 8; i++) {
            float length = sqrtf(directions[i][0] * directions[i][0] + directions[i][1] * directions[i][1]);
            float dx = directions[i][0] / length, dy = directions[i][1] / length;
            float tx = 24 + dx * 18, ty = 24 + dy * 18;
            points[i][0].x = lroundf(tx - dx * 11 - dy * 8);
            points[i][0].y = lroundf(ty - dy * 11 + dx * 8);
            points[i][1].x = lroundf(tx);
            points[i][1].y = lroundf(ty);
            points[i][2].x = lroundf(tx - dx * 11 + dy * 8);
            points[i][2].y = lroundf(ty - dy * 11 - dx * 8);
            points[i][3] = points[i][1];
            points[i][4].x = lroundf(24 - dx * 18);
            points[i][4].y = lroundf(24 - dy * 18);
        }
        initialized = true;
    }
    lv_obj_delete(lv_obj_get_child(button, 0));
    lv_obj_t *line = lv_line_create(button);
    lv_line_set_points(line, points[direction], 5);
    lv_obj_set_size(line, 48, 48);
    lv_obj_set_style_line_color(line, lv_color_hex(WHITE), 0);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(line);
}
static void wcs(lv_event_t *e) {
    result(runtime_action(Action::Wcs, nullptr,
                          54 + lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e))));
}
static lv_obj_t *dropdown(lv_obj_t *parent, const char *options, int selected, int x, int y, int width,
                          lv_event_cb_t cb) {
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, width, 48);
    lv_dropdown_set_options(d, options);
    lv_dropdown_set_selected(d, selected);
    lv_obj_set_style_bg_color(d, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(d, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_text_color(d, lv_color_hex(TEXT), 0);
    lv_obj_set_style_text_color(d, lv_color_hex(TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(d, lv_color_hex(MUTED), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(d, lv_color_hex(MUTED), LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(d, LV_GRAD_DIR_NONE, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_text_font(d, font(18), 0);
    lv_obj_t *list = lv_dropdown_get_list(d);
    lv_obj_set_style_bg_color(list, lv_color_hex(CARD), 0);
    lv_obj_set_style_text_color(list, lv_color_hex(TEXT), 0);
    lv_obj_set_style_text_font(list, font(20), 0);
    lv_obj_set_style_pad_ver(list, 14, LV_PART_ITEMS);
    if (cb)
        lv_obj_add_event_cb(d, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return d;
}
static void step_changed(lv_event_t *e) {
    step_index = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    save();
}
static void units_changed(lv_event_t *e) {
    inches = (uintptr_t)lv_event_get_user_data(e);
    save();
    build_page();
}
static void connection_changed(lv_event_t *e) {
    bool usb = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1;
    if (usb == usb_connection || state.sd_active || state.busy || state.probing || state.uploading || state.cycle_axis >= 0) return;
    runtime_disconnect(); usb_connection = usb; save();
    int scroll = lv_obj_get_scroll_y(body); build_page();
    lv_obj_scroll_to_y(body, scroll, LV_ANIM_OFF);
}
static void connect_click(lv_event_t *) {
    if (runtime_link_active() || state.online)
        runtime_disconnect();
    else
        result(runtime_connect((int)prefs.baud, usb_connection));
}
static void demo_click(lv_event_t *) { runtime_demo(!runtime_is_demo()); }
static bool unlock() { return runtime_action(Action::Unlock); }
static bool acknowledge() { return runtime_action(Action::Acknowledge); }
static void fault_message(char *text,size_t size) {
    bool alarm=state.alarm!=0;
    int code=alarm ? state.alarm : state.error;
    bool described=state.fault_description_code==code && state.fault_description_alarm==alarm && *state.fault_description;
    const char *detail=described ? state.fault_description : state.message;
    const char *instruction=alarm && !state.job_failed && !state.macro_failed ?
        (critical_alarm(code) ? "\n\nRemove the cause first. Reset, wait, then unlock. Position may be lost." :
                               "\n\nRemove the cause first. Unlock does not restore a lost position.") : "";
    snprintf(text,size,"%s%s",detail,instruction);
}
static void recover(lv_event_t *) {
    char title[64];
    char description[512]; fault_message(description,sizeof description);
    fault_confirmation_id=state.alarm ? (state.alarm<0 ? 1000 : state.alarm) : -state.error;
    if ((state.macro_failed || state.job_failed) && (state.alarm || state.error)) {
        snprintf(title, sizeof title, "%s %d / Stop %s?", state.alarm ? "ALARM" : "ERROR", state.alarm ? state.alarm : state.error, state.macro_failed ? "macro" : "job");
        confirm(title, description, stop, RED, "Stop", RED); fault_confirmation=true;
        return;
    }
    if (state.alarm) {
        bool critical = critical_alarm(state.alarm);
        if (state.alarm < 0) snprintf(title, sizeof title, "Machine locked / Unlock?");
        else snprintf(title, sizeof title, "ALARM %d / %s?", state.alarm, critical ? "Reset & unlock" : "Unlock");
        confirm(title,description,unlock, RED, critical ? "Reset & unlock" : "Unlock"); fault_confirmation=true;
    } else if (state.error) {
        snprintf(title, sizeof title, "ERROR %d / Acknowledge?", state.error);
        confirm(title, description, acknowledge, RED, "OK"); fault_confirmation=true;
    }
    else
        notify("No active alarm or command error.");
}
static void navigate(lv_event_t *e) {
    runtime_snapshot(state);
    if ((state.macro_running || state.macro_failed) && state.online) {
        if (lv_tabview_get_tab_active(tabview) != 2) lv_tabview_set_active(tabview, 2, LV_ANIM_OFF);
        return;
    }
    if (state.sd_active || page == JobPage || page == CompletePage)
        return;
    int target_tab = lv_tabview_get_tab_active(tabview);
    int current_tab = tab_for_page(page);
    int target = tab_pages[target_tab];
    if (target == page)
        return;
    int direction = target_tab > current_tab ? 1 : -1;
    close_modal();
    preview_show = false;
    show_page(target);
    parent_page_content(page_layer);
    lv_anim_t slide;
    lv_anim_init(&slide);
    lv_anim_set_var(&slide, page_layer);
    lv_anim_set_exec_cb(&slide, [](void *obj, int32_t x) { lv_obj_set_x((lv_obj_t *)obj, x); });
    lv_anim_set_values(&slide, direction * 1024, 0);
    lv_anim_set_duration(&slide, 220);
    lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&slide, [](lv_anim_t *) { parent_page_content(root); });
    lv_anim_start(&slide);
}
static void visibility(lv_obj_t *o, bool visible) {
    if (visible)
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void layout_dro() {
    bool jog = page == JogPage && !actuator_mode, job = page == JobPage;
    for (int i = 0; i < 3; i++) {
        visibility(axis_card[i], jog || job);
        lv_obj_set_pos(axis_card[i], 16, 90 + i * 72);
        lv_obj_set_size(axis_card[i], 472, 64);
        lv_obj_set_pos(axis_name[i], jog ? 94 : 20, 21);
        lv_obj_set_pos(axis_home[i], 8, 6);
        lv_obj_set_size(axis_home[i], 76, 52);
        lv_obj_set_pos(axis_zero[i], 386, 6);
        lv_obj_set_size(axis_zero[i], 78, 52);
        visibility(axis_home[i], jog);
        visibility(axis_zero[i], jog);
        lv_obj_set_pos(axis_machine[i], 120, 24);
        lv_obj_set_width(axis_machine[i], 104);
        lv_obj_set_pos(axis_work[i], jog ? 230 : 260, jog ? 16 : 18);
        lv_obj_set_width(axis_work[i], jog ? 148 : 192);
        lv_obj_set_style_text_font(axis_work[i], font(jog ? 28 : 22), 0);
    }
    visibility(machine_heading, jog || job);
    visibility(work_heading, jog || job);
    visibility(left_surface, jog || job);
    visibility(right_surface, jog || job);
    visibility(tabview, tab_for_page(page) >= 0);
    visibility(job_controls, job);
    layout_stop();
    visibility(dro_actions, jog);
    visibility(jog_tools, jog);
}
static void job_ok(lv_event_t *) {
    runtime_snapshot(state);
    if (state.sd_active)
        return;
    result(runtime_action(Action::JobAcknowledged));
    show_page(JogPage);
}
static void override_changed(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    bool spindle = (uintptr_t)lv_event_get_user_data(e);
    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
        return;
    if (now() - override_sent < 600)
        return;
    int value = lv_slider_get_value(slider);
    result(runtime_action(spindle ? Action::SpindleOverride : Action::FeedOverride, nullptr, value));
    override_sent = now();
}
static lv_obj_t *override_control(const char *title, int y, bool spindle, lv_obj_t **text) {
    *text = label(body, title, 0, y, 480, 20);
    lv_obj_t *slider = lv_slider_create(body);
    lv_obj_set_pos(slider, 20, y + 58);
    lv_obj_set_size(slider, 452, 18);
    lv_slider_set_range(slider, 10, 200);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(TRACK), 0);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(PRIMARY), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 12, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, override_changed, LV_EVENT_RELEASED, (void *)(uintptr_t)spindle);
    return slider;
}
static void wifi_result(esp_err_t err) {
    if (err != ESP_OK)
        notify(esp_err_to_name(err));
}
static void wifi_scan(lv_event_t *) { wifi_result(wifi_bridge_scan()); }
static void wifi_disconnect(lv_event_t *) { wifi_result(wifi_bridge_disconnect()); }
static void wifi_connect(lv_event_t *) {
    const char *password = lv_textarea_get_text(wifi_password);
    if (wifi_auth && strlen(password) < 8) {
        lv_label_set_text(wifi_password_error, "Use at least 8 characters");
        return;
    }
    esp_err_t err = wifi_bridge_connect(wifi_ssid, password);
    if (err != ESP_OK) {
        lv_label_set_text(wifi_password_error, esp_err_to_name(err));
        return;
    }
    wifi_attempt = true;
    show_wifi();
}
static void wifi_password_visible(lv_event_t *e) {
    lv_obj_t *check = (lv_obj_t *)lv_event_get_target(e);
    lv_textarea_set_password_mode(wifi_password, !lv_obj_has_state(check, LV_STATE_CHECKED));
}
static void wifi_choose(lv_event_t *e) {
    unsigned i = (uintptr_t)lv_event_get_user_data(e);
    if (i >= wifi.count || wifi.scanning || wifi.connecting) return;
    snprintf(wifi_ssid, sizeof wifi_ssid, "%s", wifi.networks[i].ssid);
    wifi_auth = wifi.networks[i].authmode;
    char title[72]; snprintf(title,sizeof title,"Connect to Wi-Fi: %s",wifi_ssid);
    auto c = open_modal(title);
    modal_close_button(c,1024,show_wifi);
    label(c,"Password",40,120,112,18,MUTED);
    wifi_password = lv_textarea_create(c);
    lv_obj_set_pos(wifi_password,160,100); lv_obj_set_size(wifi_password,440,64);
    lv_textarea_set_one_line(wifi_password,true); lv_obj_set_height(wifi_password,64);
    lv_textarea_set_password_mode(wifi_password,true);
    lv_textarea_set_max_length(wifi_password,64); lv_textarea_set_password_show_time(wifi_password,0);
    lv_textarea_set_placeholder_text(wifi_password,wifi_auth?"Password":"Open network");
    lv_obj_set_style_text_font(wifi_password,font(22),0);
    button(c,"Cancel",616,100,168,64,show_wifi,0,RED);
    button(c,"Connect",800,100,184,64,wifi_connect,0,GREEN);
    auto check=lv_checkbox_create(c); lv_obj_set_pos(check,40,184);
    lv_checkbox_set_text(check,"Show password");
    lv_obj_add_event_cb(check,wifi_password_visible,LV_EVENT_VALUE_CHANGED,nullptr);
    wifi_password_error=label(c,"",300,184,684,16,RED);
    auto keyboard=lv_keyboard_create(c); lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,40,236); lv_obj_set_size(keyboard,944,324);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard,wifi_password);
    finish_modal(c);
}

static void wifi_networks() {
    lv_obj_clean(wifi_list);
    for (unsigned i=0;i<wifi.count;++i) {
        auto row=button(wifi_list,"",0,i*72,936,64,wifi_choose,i,CARD);
        lv_obj_set_style_bg_grad_dir(row,LV_GRAD_DIR_NONE,0);
        lv_obj_set_style_radius(row,0,0); lv_obj_set_style_border_width(row,0,0);
        lv_obj_set_style_shadow_width(row,0,0);
        lv_obj_delete(lv_obj_get_child(row,0));
        label(row,wifi.networks[i].ssid,16,20,650,20);
        char signal[32];snprintf(signal,sizeof signal,"%d dBm",wifi.networks[i].rssi);
        auto strength=label(row,signal,692,22,168,16,MUTED);
        lv_obj_set_style_text_align(strength,LV_TEXT_ALIGN_RIGHT,0);
        if(wifi.networks[i].authmode) {
            auto lock=lv_image_create(row); lv_image_set_src(lock,&panel_wifi_lock);
            lv_obj_set_pos(lock,884,22); lv_obj_set_style_image_recolor(lock,lv_color_hex(0x94A0A5),0);
            lv_obj_set_style_image_recolor_opa(lock,LV_OPA_COVER,0);
        }
        form_control_style(row);
        lv_obj_set_style_bg_color(row,lv_color_hex(0x080B0D),0);
        lv_obj_set_style_radius(row,0,0);
    }
    wifi_revision=wifi.revision;
}
static void show_wifi(lv_event_t *) {
    wifi_bridge_snapshot(&wifi);
    if(!wifi.scanning&&!wifi.connecting&&!wifi.count) wifi_scan(nullptr);
    auto c=open_modal("Wi-Fi networks"); modal_close_button(c);
    wifi_scan_btn=button(c,"Scan",644,100,148,56,wifi_scan);
    wifi_disconnect_btn=button(c,"Disconnect",808,100,176,56,wifi_disconnect,0,RED);
    wifi_status=label(c,"",88,116,536,18);
    lv_label_set_long_mode(wifi_status,LV_LABEL_LONG_DOT);
    wifi_spinner=lv_spinner_create(c); lv_obj_set_pos(wifi_spinner,40,108);lv_obj_set_size(wifi_spinner,32,32);
    wifi_list=box(c,40,180,944,380,CARD);
    lv_obj_set_style_border_width(wifi_list,0,0);lv_obj_set_style_radius(wifi_list,0,0);
    lv_obj_add_flag(wifi_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(wifi_list,LV_DIR_VER);
    wifi_networks();refresh(nullptr);finish_modal(c);
}

static void wifi_result_ok(lv_event_t *) {
    close_modal();
    build_page();
}
static void wifi_notification() {
    auto c=open_modal(wifi.connected?"Successfully connected":"Could not connect",1024,600,wifi.connected?GREEN:RED);
    char message[192];
    if(wifi.connected) snprintf(message,sizeof message,"%s\nIP address: %s",wifi.ssid,wifi.ip);
    else snprintf(message,sizeof message,"%s\n%s",wifi.ssid,wifi.status);
    auto text=label(c,message,80,164,864,28,wifi.connected?GREEN:RED);
    lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);
    button(c,"OK",660,488,324,64,wifi_result_ok,0,GREEN);finish_modal(c);
}

static void wifi_refresh_ui() {
    wifi_bridge_snapshot(&wifi);
    const char *name = wifi.connected ? wifi.ssid : wifi.connecting ? "Connecting..." : "Not connected";
    const char *ip = wifi.connected ? wifi.ip : "Wi-Fi";
    if (!modal && strcmp(lv_label_get_text(wifi_header_name), name)) lv_label_set_text(wifi_header_name, name);
    if (!modal && strcmp(lv_label_get_text(wifi_header_ip), ip)) lv_label_set_text(wifi_header_ip, ip);
    lv_color_t icon_color = lv_color_hex(wifi.connected ? WHITE : MUTED);
    if (!lv_color_eq(lv_obj_get_style_text_color(wifi_icon, 0), icon_color))
        lv_obj_set_style_text_color(wifi_icon, icon_color, 0);
    lv_opa_t icon_opa = !wifi.connected || !wifi.rssi || wifi.rssi >= -60 ? LV_OPA_COVER :
                        wifi.rssi >= -75 ? LV_OPA_80 : LV_OPA_50;
    if (lv_obj_get_style_opa(wifi_icon, 0) != icon_opa) lv_obj_set_style_opa(wifi_icon, icon_opa, 0);
    if (wifi_attempt && !wifi.connecting && tab_for_page(page) >= 0) {
        wifi_attempt = false;
        wifi_notification();
    }
    char network_status[160];
    snprintf(network_status, sizeof network_status, wifi.connected && !wifi.scanning ? "%s / %s" : "%s",
             wifi.connected && !wifi.scanning ? wifi.ssid : wifi.status, wifi.ip);
    if (wifi_row_status && !modal)
        lv_label_set_text(wifi_row_status, network_status);
    if (wifi_status) {
        if (strcmp(lv_label_get_text(wifi_status),network_status)) lv_label_set_text(wifi_status, network_status);
        visibility(wifi_spinner, wifi.scanning || wifi.connecting);
        enable(wifi_scan_btn, !wifi.scanning && !wifi.connecting);
        enable(wifi_disconnect_btn, wifi.connected && !wifi.scanning && !wifi.connecting);
        if (wifi_revision != wifi.revision)
            wifi_networks();
        enable(wifi_list, !wifi.scanning && !wifi.connecting);
    }
}
static lv_obj_t *settings_list_row(lv_obj_t *parent, int y, int height) {
    auto row = box(parent,0,y,970,height,CARD);
    lv_obj_set_style_radius(row,0,0);
    lv_obj_set_style_shadow_width(row,0,0);
    lv_obj_set_style_border_side(row,LV_BORDER_SIDE_BOTTOM,0);
    return row;
}
static void setting_action_row(lv_obj_t *parent, const char *title, const char *action, int y, lv_event_cb_t open) {
    auto row = settings_list_row(parent,y,76);
    label(row, title, 20, 25, 620, 20);
    button(row, action, 680, 10, 270, 56, open);
}
static void setting_row(lv_obj_t *parent, const char *title, float value, int y, int id) {
    lv_obj_t *row = settings_list_row(parent,y,76);
    label(row, title, 20, 25, 620, 20);
    char text[48];
    snprintf(text, sizeof text, "%.*f%s%s", id >= 8 ? 0 : !inches && (id == 3 || id == 4 || id == 5 || id == 7) ? 1 : 3,
             value / (id < 8 ? display_scale(inches) : 1), id >= 8 ? "" : " ",
             id >= 8 ? "" : inches ? (id == 6 ? "in/min" : "in") : (id == 6 ? "mm/min" : "mm"));
    button(row, text, 680, 10, 270, 56, edit_value, id);
}
#include "panel_actuators_ui.inc"
#include "panel_settings_ui.inc"
#include "panel_tools_ui.inc"
#include "panel_storage_ui.inc"
#include "panel_probe_ui.inc"
#include "panel_probe_config_ui.inc"
#include "panel_jog_concept.inc"
static void probe_refresh_ui() {
    bool on = ready();
    char s[160];
    if (probe_start) {
        if (state.probing) {
            if (state.probe_axis >= 0) snprintf(s, sizeof s, "Probing %c", "XYZ"[state.probe_axis]);
            else snprintf(s, sizeof s, "Probing axis %d / 3", state.probe_stage < 3 ? state.probe_stage + 1 : 3);
            lv_label_set_text(probe_status, s);
        } else
            lv_label_set_text(probe_status, state.probe           ? "Contact detected - release the plate"
                                            : state.contact_ready ? "Contact verified"
                                            : state.probe_known   ? "Test probe: lift plate, touch tool, then release"
                                                                  : "Waiting for probe feedback");
        lv_obj_set_style_text_color(probe_status, lv_color_hex(state.contact_ready ? GREEN : 0x8A5500), 0);
        if (contact_btn) lv_label_set_text(lv_obj_get_child(contact_btn, 0), state.contact_testing ? "Touch tool to plate, then release" : "Test probe");
        enable(contact_btn, on && state.probe_known && !state.contact_testing && state.spindle == 0 && state.spindle_direction == 0);
        enable(probe_start, on && state.contact_ready && !state.probe && state.spindle == 0 && state.spindle_direction == 0 &&
                                ((single_probe && surface_axes[probe_surface_choice] == 2) || prefs.probe.diameter > 0) && state.modal_known && state.wco_known);
    }
}
static void build_page() {
    jog_control_key = UINT32_MAX;
    storage_preview_viewing(false);
    runtime_snapshot(state);
    if (state.cycle_axis >= 0)
        runtime_action(Action::JogCancel);
    lv_anim_delete(page_layer, nullptr);
    lv_obj_set_x(page_layer, 0);
    parent_page_content(root);
    concept_restore();
    if (page_title) { lv_obj_delete(page_title); page_title = nullptr; }
    release_jog();
    jog_count = 0;
    wifi_row_status = nullptr;
    usb_connection_status = controller_connect = controller_demo = nullptr;
    memset(file_run_buttons,0,sizeof file_run_buttons);
    file_previous=file_next=file_page_label=nullptr;
    file_box = file_title = progress = progress_text = time_text = start_btn = refresh_btn = nullptr;
    feed_slider = spindle_slider = feed_text = spindle_text = nullptr;
    speed_choices = mode_choices = step_dropdown = nullptr;
    memset(act_position, 0, sizeof act_position);
    memset(act_travel, 0, sizeof act_travel);
    memset(act_slider, 0, sizeof act_slider);
    memset(act_home, 0, sizeof act_home);
    memset(act_move, 0, sizeof act_move);
    memset(act_stop, 0, sizeof act_stop);
    gcode_viewer_detach();
    lv_obj_clean(body);
    file_prepare_status=file_prepare_spinner=file_prepare_name=nullptr;
    brightness_value = nullptr;
    free(cached_pixels);cached_pixels=nullptr;cached_image=cached_status=nullptr;cached_loaded=false;
    runtime_snapshot(state);
    layout_dro();
    visibility(nav[1], !actuator_mode);
    visibility(nav[2], !actuator_mode);
    lv_label_set_text(lv_obj_get_child(nav[0], 0), actuator_mode ? "Actuators" : "Jog");
    bool compact = (page == JogPage && !actuator_mode) || page == JobPage;
    lv_obj_set_pos(body, compact ? 508 : 16, 76);
    lv_obj_set_size(body, compact ? 500 : 992, 424);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    if (tab_for_page(page) >= 0)
        lv_tabview_set_active(tabview, tab_for_page(page), LV_ANIM_OFF);
    bool floating = page == JogPage || page == JobPage;
    lv_obj_set_style_radius(body,page == SettingsPage ? 0 : 16,0);
    lv_obj_set_style_shadow_width(body,page == SettingsPage ? 0 : 14,0);
    lv_obj_set_style_bg_color(body, lv_color_hex(CARD), 0);
    lv_obj_set_style_bg_opa(body, floating ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_opa(body, floating ? LV_OPA_TRANSP : LV_OPA_10, 0);
    if (page == JogPage && actuator_mode) {
        build_actuators();
    } else if (page == 0) {
        label(body, "JOG FEEDRATE", 0, 0, 270, 12, MUTED);
        label(body, "JOG DISTANCE", 282, 0, 218, 12, MUTED);
        static const char *const speeds[] = {"Fast", "Med", "Slow", ""};
        static const char *const modes[] = {"Incremental", "Continuous", ""};
        speed_choices = choices(body, speeds, jog_speed, 0, 20, 260, 48, speed_click);
        mode_choices = choices(body, modes, continuous ? 1 : 0, 0, 80, 500, 56, jog_mode);
        step_dropdown = dropdown(body, inches ? "0.01 in\n0.1 in\n1 in\n10 in" : "0.1 mm\n1 mm\n10 mm\n100 mm",
                                 step_index, 282, 20, 218, step_changed);
        const char *names[] = {
            LV_SYMBOL_LEFT " " LV_SYMBOL_UP,   "Y+", LV_SYMBOL_UP " " LV_SYMBOL_RIGHT,   "X-", "X+",
            LV_SYMBOL_LEFT " " LV_SYMBOL_DOWN, "Y-", LV_SYMBOL_DOWN " " LV_SYMBOL_RIGHT, "Z+", "Z-"};
        const int cells[] = {0, 1, 2, 3, 5, 6, 7, 8};
        const uint32_t colors[] = {XY_COLOR, AXIS_COLORS[1], XY_COLOR, AXIS_COLORS[0], AXIS_COLORS[0],
                                   XY_COLOR, AXIS_COLORS[1], XY_COLOR, AXIS_COLORS[2], AXIS_COLORS[2]};
        for (int i = 0; i < 10; i++) {
            int x = 16 + (i < 8 ? (cells[i] % 3) * 116 : 360);
            int y = i < 8 ? 152 + (cells[i] / 3) * 90 : (i == 8 ? 152 : 288);
            lv_obj_t *b = button(body, names[i], x, y, 108, i < 8 ? 80 : 124, nullptr, 0,
                                 colors[i]);
            lv_obj_set_style_text_font(lv_obj_get_child(b, 0), font(28), 0);
            if (i < 8)
                jog_arrow(b, i);
            lv_obj_add_event_cb(b, jog_event, LV_EVENT_ALL, (void *)(uintptr_t)i);
            jog_buttons[jog_count++] = b;
        }
    } else if (page == FilesPage && local_preview_show && storage_source()) {
        build_cached_preview();
    } else if (page == FilesPage && preview_show && state.download_ready) {
        lv_obj_t *name = label(body, preview_path, 84, 26, 660, 20);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        navigation_back(body, preview_back);
        lv_obj_t *canvas = box(body, 16, 76, 740, 328);
        lv_obj_set_style_radius(canvas, 0, 0);
        lv_obj_set_style_border_width(canvas, 0, 0);
        gcode_viewer_attach(canvas, 740, 328);
        button(body, "Top", 772, 90, 96, 64, view_action, 0);
        button(body, "3D", 880, 90, 96, 64, view_action, 1);
        start_btn = button(body, "Run from SD", 772, 330, 204, 64, start_click, 0, GREEN);
    } else if (page == FilesPage) {
        file_prepare_spinner = lv_spinner_create(body);
        lv_obj_set_pos(file_prepare_spinner, 16, 18);
        lv_obj_set_size(file_prepare_spinner, 28, 28);
        lv_spinner_set_anim_params(file_prepare_spinner, 1000, 220);
        file_prepare_status = label(body, "", 56, 24, 690, 16, MUTED);
        file_prepare_name = label(body, "", 56, 4, 690, 14, TEXT);
        refresh_btn = button(body, "Refresh", 768, 12, 208, 48, list_click);
        label(body, "FILE", 16, 58, 500, 12, MUTED);
        label(body, "SIZE", 552, 58, 120, 12, MUTED);
        file_previous=button(body,"Previous",16,380,180,44,file_page_changed,(uintptr_t)-1);
        file_next=button(body,"Next",796,380,180,44,file_page_changed,1);
        file_page_label=label(body,"",386,392,220,18,MUTED);
        lv_obj_set_style_text_align(file_page_label,LV_TEXT_ALIGN_CENTER,0);
        file_box = box(body, 0, 80, 992, 288, CARD);
        lv_obj_add_flag(file_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(file_box, LV_DIR_VER);
        files();
    } else if (page == MacrosPage) {
        build_macros();
    } else if (page == DiagnosticsPage) {
        build_inputs();
    } else if (page == SettingsPage) {
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_t *application = settings_list_row(body, 44, 80);
        label(application, "Mode", 20, 28, 500, 20);
        static const char *const applications[] = {"CNC", "Actuator", ""};
        choices(application, applications, actuator_mode, 680, 16, 270, 48, application_changed);
        auto location = settings_list_row(body, 140, 80);
        label(location, "File location", 20, 28, 600, 20);
        dropdown(location, "Onboard SD\nController SD", storage_source() ? 0 : 1,
                 680, 16, 270, file_location_changed);
        auto light = settings_list_row(body, 236, 80);
        label(light, "Screen brightness", 20, 28, 550, 20);
        char light_text[16]; snprintf(light_text, sizeof light_text, "%u%%", screen_brightness);
        brightness_value = label(light, light_text, 600, 28, 70, 18);
        auto slider = lv_slider_create(light);
        lv_obj_set_pos(slider, 690, 34); lv_obj_set_size(slider, 250, 12);
        lv_slider_set_range(slider, 10, 100);
        lv_slider_set_value(slider, screen_brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(TRACK), 0);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(slider, lv_color_hex(PRIMARY), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(PRIMARY), LV_PART_KNOB);
        lv_obj_add_event_cb(slider, brightness_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(slider, brightness_changed, LV_EVENT_RELEASED, nullptr);
        lv_obj_t *units = settings_list_row(body, 332, 80);
        label(units, "Display units", 20, 28, 500, 20);
        auto metric = button(units, "mm", 680, 12, 130, 56, units_changed, 0, !inches ? PRIMARY : EDGE);
        auto imperial = button(units, "inch", 820, 12, 130, 56, units_changed, 1, inches ? PRIMARY : EDGE);
        lv_obj_add_state(inches ? imperial : metric, LV_STATE_CHECKED);
        lv_obj_t *network = settings_list_row(body, 428, 88);
        label(network, "Wi-Fi", 20, 12, 620, 20);
        wifi_row_status = label(network, "", 20, 46, 620, 14, MUTED);
        button(network, "Networks", 680, 16, 270, 56, show_wifi);
        setting_action_row(body, "Preview server", "Configure", 532, show_preview_server);
        setting_action_row(body, "Machine settings", "Configure", 620, show_machine_settings);
        label(body, actuator_mode ? "Actuators" : "Probe", 0, 724, 960, 18, MUTED);
        if (actuator_mode) {
            const char *styles[] = {"Buttons", "Position slider", "Three presets", "Repeat / speed slider"};
            for (int i = 0; i < 4; i++) {
                auto row = settings_list_row(body, 756 + i * 88, 76);
                char title[80];
                snprintf(title, sizeof title, "%s / %c", actuator_prefs.axes[i].name, "XYZA"[i]);
                label(row, title, 20, 10, 600, 20);
                label(row, i == 3 && a_rotary ? "Rotary / degrees" : styles[actuator_prefs.axes[i].style], 20, 42, 600, 14, MUTED);
                button(row, "Configure", 680, 10, 270, 56, actuator_config, i);
            }
        } else {
            setting_action_row(body, "Probe configuration", "Edit", 756, show_probe_offsets);
        }
        int controller_y = actuator_mode ? 1220 : 868;
        label(body, "Controller connection", 0, controller_y, 960, 18, MUTED);
        auto connection_row = settings_list_row(body, controller_y + 32, 76);
        label(connection_row, "Connection", 20, 25, 620, 20);
        static const char *const connections[] = {"UART", "USB3", ""};
        auto connection = choices(connection_row, connections, usb_connection, 680, 12, 270, 52, connection_changed);
        enable(connection, !state.sd_active && !state.busy && !state.probing && !state.uploading && state.cycle_axis < 0);
        if (usb_connection) {
            auto row = settings_list_row(body, controller_y + 120, 76);
            label(row, "USB controller", 20, 25, 420, 20);
            char status[80]; usb_status(status, sizeof status);
            usb_connection_status = label(row, status, 460, 25, 490, 18, MUTED);
            lv_obj_set_style_text_align(usb_connection_status, LV_TEXT_ALIGN_RIGHT, 0);
        } else {
            setting_row(body, "Baud rate", prefs.baud, controller_y + 120, 10);
        }
        int buttons_y = controller_y + 216;
        controller_connect = button(body, runtime_link_active() || state.online ? "Disconnect" : "Connect controller", 0, buttons_y, 472, 64, connect_click);
        controller_demo = button(body, runtime_is_demo() ? "Exit preview" : "Preview / controller off", 498, buttons_y, 472, 64, demo_click);
    } else if (page == JobPage) {
        file_title = label(body, state.filename, 0, 0, 500, 20);
        lv_obj_set_style_text_align(file_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_height(file_title, 26);
        lv_label_set_long_mode(file_title, LV_LABEL_LONG_DOT);
        progress_text = label(body, "", 238, 48, 160, 32, PRIMARY);
        time_text = label(body, "", 238, 96, 160, 20);
        lv_obj_set_style_text_align(progress_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(time_text, LV_TEXT_ALIGN_CENTER, 0);
        progress = lv_arc_create(body);
        lv_obj_set_pos(progress, 106, 36);
        lv_obj_set_size(progress, 100, 100);
        lv_arc_set_rotation(progress, 270);
        lv_arc_set_bg_angles(progress, 0, 360);
        lv_arc_set_range(progress, 0, 100);
        lv_obj_remove_style(progress, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(progress, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(progress, 7, 0);
        lv_obj_set_style_arc_width(progress, 7, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(progress, lv_color_hex(LINE), 0);
        lv_obj_set_style_arc_color(progress, lv_color_hex(PRIMARY), LV_PART_INDICATOR);
        feed_slider = override_control("Feed override", 176, false, &feed_text);
        spindle_slider = override_control("Speed override", 286, true, &spindle_text);

    } else if (page == CompletePage) {
        lv_obj_set_style_bg_color(body, lv_color_hex(GREEN), 0);
        label(body, LV_SYMBOL_OK "  Job Complete", 140, 80, 712, 32, WHITE);
        label(body, state.filename, 140, 144, 712, 22, WHITE);
        button(body, "OK", 290, 250, 412, 80, job_ok, 0, CARD);
    }
    concept_layout();
    refresh(nullptr);
}
static void refresh_dro(bool colors = true) {
    char work[32], machine[32];
    for (int i = 0; i < 3; ++i) {
        if (state.coordinates && state.wco_known) {
            snprintf(work, sizeof work, "% .3f", state.work[i] / display_scale(inches));
            snprintf(machine, sizeof machine, concept_active() ? "M  % .3f" : "% .3f", state.machine[i] / display_scale(inches));
        } else {
            snprintf(work, sizeof work, "--.---");
            snprintf(machine, sizeof machine, "M  --.---");
        }
        if (strcmp(lv_label_get_text(axis_work[i]), work)) lv_label_set_text(axis_work[i], work);
        if (strcmp(lv_label_get_text(axis_machine[i]), machine)) lv_label_set_text(axis_machine[i], machine);
        if (colors) lv_obj_set_style_text_color(axis_work[i], lv_color_hex(state.status ? TEXT : MUTED), 0);
    }
}
static void refresh_jog_data() {
    refresh_dro(false);
    char text[64];
    snprintf(text, sizeof text, "%s%s", runtime_is_demo() ? "PREVIEW / " : "", state.mode);
    if (strcmp(lv_label_get_text(state_label), text)) lv_label_set_text(state_label, text);
    if (!lv_dropdown_is_open(wcs_dropdown) && state.wcs >= 54 && state.wcs <= 59 &&
        lv_dropdown_get_selected(wcs_dropdown) != (unsigned)(state.wcs - 54))
        lv_dropdown_set_selected(wcs_dropdown, state.wcs - 54);
    wifi_refresh_ui();
}
static void refresh(lv_timer_t *) {
    runtime_snapshot(state);
    hardware_status(state);
    if (fault_confirmation && confirmation_message) {
        int active=state.alarm ? (state.alarm<0 ? 1000 : state.alarm) : -state.error;
        if (active==fault_confirmation_id) {
            char description[512]; fault_message(description,sizeof description);
            if (strcmp(lv_label_get_text(confirmation_message),description)) lv_label_set_text(confirmation_message,description);
        }
        if ((state.busy && confirmation!=stop) || state.recovering) lv_obj_add_state(confirmation_action,LV_STATE_DISABLED);
        else lv_obj_remove_state(confirmation_action,LV_STATE_DISABLED);
    }
    if (state.sd_active && page != JobPage) {
        close_modal();
        show_page(JobPage);
        return;
    }
    if (state.job_complete && page != CompletePage) {
        close_modal();
        show_page(CompletePage);
        return;
    }
    if (modal) {
        int fault = state.alarm ? (state.alarm < 0 ? 1000 : state.alarm) : -state.error;
        if (!fault) fault_seen = 0;
        if (fault && fault != fault_seen && !state.recovering) { fault_seen = fault; recover(nullptr); return; }
    }
    if (wifi_status) { wifi_refresh_ui(); return; }
    if (wifi_password || number || confirmation || actuator_config_open) return;
    if (probe_status) { probe_refresh_ui(); return; }
    if (preview_update_open) { preview_update_refresh(); return; }
    if (server_open || probe_offsets_open) return;
    if (accessories_open || spindles_open || macro_editor_open) {
        int fault = state.alarm ? (state.alarm < 0 ? 1000 : state.alarm) : -state.error;
        if (!fault) fault_seen = 0;
        if (fault && fault != fault_seen && !state.recovering) { fault_seen = fault; recover(nullptr); }
        else tools_refresh();
        return;
    }
    if (machine_browser || machine_editor || machine_search) {
        int fault = state.alarm ? (state.alarm < 0 ? 1000 : state.alarm) : -state.error;
        if (!fault) fault_seen = 0;
        if (fault && fault != fault_seen && !state.recovering) { fault_seen = fault; recover(nullptr); }
        else machine_refresh();
        return; // The full-screen settings view does not need hidden Jog/Job redraws.
    }
    if (modal) return; // Full-screen notices do not redraw the covered main page.
    if (page == SettingsPage && !modal && controller_connect) {
        auto set = [](lv_obj_t *o, const char *text) { if (o && strcmp(lv_label_get_text(o), text)) lv_label_set_text(o, text); };
        set(lv_obj_get_child(controller_connect,0), runtime_link_active() || state.online ? "Disconnect" : "Connect controller");
        set(lv_obj_get_child(controller_demo,0), runtime_is_demo() ? "Exit preview" : "Preview / controller off");
        if (usb_connection_status) { char text[80]; usb_status(text,sizeof text); set(usb_connection_status,text); }
    }
    int parsed = preview_parsing.load();
    if (parsed == 2 || parsed == 3) {
        preview_parsing.store(0);
        preview_show = parsed == 2 && state.download_ready && !strcmp(preview_path, state.download_path);
        if (parsed == 3)
            notify(preview_message);
        if (page == 1) {
            build_page();
            return;
        }
    }
    if (preview_pending && !state.downloading && !state.busy) {
        preview_pending = false;
        if (state.download_ready && !strcmp(preview_path, state.download_path) &&
            runtime_preview_acquire(&preview_data, &preview_size)) {
            preview_parsing.store(1);
            if (xTaskCreate(parse_preview, "preview_parse", 8192, nullptr, 2, nullptr) != pdPASS) {
                runtime_preview_release();
                preview_parsing.store(0);
                notify("Not enough memory to parse preview");
            }
        }
    }
    if (preview_show && !state.download_ready) {
        preview_show = false;
        if (page == 1) {
            build_page();
            return;
        }
    }
    if(page==FilesPage && local_preview_show)cached_refresh();
    bool cnc_jog = page == JogPage && !actuator_mode;
    unsigned known_rate_axes = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (isfinite(state.axis_max_rate[i]) && state.axis_max_rate[i] > 0) known_rate_axes |= 1u << i;
    bool manual_motion = cnc_jog && state.online && state.identified && state.status &&
                         !state.alarm && !state.error && !state.recovering && !state.sd_active && !state.probing && !state.uploading &&
                         !state.macro_failed && !state.job_failed &&
                         (!strcmp(state.mode, "Idle") || !strncmp(state.mode, "Jog", 3));
    // Keep Jog controls visually steady; model actions still reject busy/non-idle commands.
    bool on = ready() || manual_motion;
    if(page==FilesPage)for(unsigned i=0;i<4;++i)tools_enable(file_run_buttons[i],on&&!state.listing&&file_run_usable[i]);
    if (page == JogPage && actuator_mode)
        refresh_actuators();
    // Keep input disabled immediately, but avoid a one-frame palette flash for brief command ACKs.
    static uint32_t busy_since;
    bool command_busy = !manual_motion && page == JogPage && state.busy && state.online && state.identified && state.status &&
                        !state.alarm && !state.error && !state.sd_active && !strcmp(state.mode, "Idle");
    if (!command_busy)
        busy_since = 0;
    else if (!busy_since)
        busy_since = now();
    bool brief_busy = command_busy && now() - busy_since < 200;
    bool steady_selection = state.online && state.identified && state.status && !state.alarm &&
                            !state.error && !state.sd_active &&
                            (!strcmp(state.mode, "Idle") || !strncmp(state.mode, "Jog", 3));
    if (cnc_jog && !state.alarm && !state.error) {
        uint32_t key = unsigned(on) | unsigned(brief_busy) << 1 | unsigned(steady_selection) << 2 |
                       unsigned(state.online) << 3 | unsigned(state.identified) << 4 | unsigned(state.status) << 5 |
                       unsigned(continuous) << 6 | unsigned(state.coordinates) << 7 | unsigned(state.wco_known) << 8 |
                       unsigned(state.modal_known) << 9 | known_rate_axes << 10;
        if (key == jog_control_key) { refresh_jog_data(); return; }
        jog_control_key = key;
        ++jog_control_updates;
    } else if (cnc_jog) jog_control_key = UINT32_MAX;
    enable(probe_btn, on, brief_busy);
    tools_enable(accessories_btn, on);
    enable_choices(speed_choices, on, steady_selection);
    enable_choices(mode_choices, on, steady_selection);
    enable(step_dropdown, on && !continuous, brief_busy && !continuous);
    char s[256];
    snprintf(s, sizeof s, "%s%s", runtime_is_demo() ? "PREVIEW / " : "", state.mode);
    lv_label_set_text(state_label, s);
    lv_obj_set_style_text_color(state_label,
                                lv_color_hex(state.alarm || state.error        ? 0xFF888D
                                             : !strncmp(state.mode, "Hold", 4) ? AMBER
                                             : state.status                    ? 0x6CDB78
                                                                               : 0xBCC6D2),
                                0);
    refresh_dro();
    if (state.alarm || state.error)
        lv_obj_add_flag(state_label, LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_remove_flag(state_label, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 3; i++)
        enable(axis_zero[i], on, brief_busy);
    enable(zero_all_btn, on, brief_busy);
    for (int i = 0; i < 2; i++)
        enable(goto_buttons[i], on && state.coordinates && state.wco_known && state.modal_known, brief_busy);
    for (int i = 0; i < jog_count; i++) {
        unsigned required = 0;
        for (unsigned axis = 0; axis < 3; ++axis) if (jog_directions[i][axis] != '0') required |= 1u << axis;
        enable(jog_buttons[i], (on || jog_held) && (known_rate_axes & required) == required, brief_busy);
    }
    bool can_home = on || (state.online && state.identified && state.status && !state.busy &&
                           !state.sd_active && !strncmp(state.mode, "Alarm", 5));
    enable(home_btn, can_home, brief_busy);
    for (int i = 0; i < 3; i++)
        enable(axis_home[i], can_home, brief_busy);
    bool held = !strncmp(state.mode, "Hold", 4);
    lv_label_set_text(lv_obj_get_child(job_pause, 0), held ? "Resume" : "Pause");
    lv_obj_set_style_bg_color(job_pause, lv_color_hex(held ? 0x83E6B0 : AMBER), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(job_pause, 0), lv_color_hex(button_text(held ? GREEN : AMBER)), 0);
    lv_obj_set_style_bg_grad_dir(job_pause, held ? LV_GRAD_DIR_NONE : LV_GRAD_DIR_VER, 0);
    enable(job_pause,
           state.online && state.status && (!strncmp(state.mode, "Run", 3) || !strcmp(state.mode, "Hold:0")));
    enable(stop_btn, state.online || state.identified);
    if (page == 1 && preview_show)
        enable(start_btn, on && !strcmp(selected, preview_path));
    if (page == FilesPage && file_box) {
        if (revision != state.file_revision)
            files();
        enable(file_box, on);
        enable(refresh_btn, storage_source()?!state.sd_active&&!state.busy:on);
    }
    if (page == JobPage && progress) {
        if(state.job_preparing)snprintf(s,sizeof s,"Checking");else snprintf(s, sizeof s, "%.1f %%", state.percent);
        lv_label_set_text(progress_text, s);
        lv_arc_set_value(progress, (int)state.percent);
        snprintf(s, sizeof s, "%02lu:%02lu:%02lu", (unsigned long)state.elapsed / 3600,
                 (unsigned long)state.elapsed / 60 % 60, (unsigned long)state.elapsed % 60);
        lv_label_set_text(time_text, s);
        snprintf(s, sizeof s, "Feed override  %d%%", state.feed_override);
        lv_label_set_text(feed_text, s);
        snprintf(s, sizeof s, "Speed override  %d%%", state.spindle_override);
        lv_label_set_text(spindle_text, s);
        if (!lv_obj_has_state(feed_slider, LV_STATE_PRESSED))
            lv_slider_set_value(feed_slider, state.feed_override, LV_ANIM_OFF);
        if (!lv_obj_has_state(spindle_slider, LV_STATE_PRESSED))
            lv_slider_set_value(spindle_slider, state.spindle_override, LV_ANIM_OFF);
        bool overrides = state.online && state.status && state.sd_active && state.overrides_known &&
                         !state.alarm && !state.error;
        enable(feed_slider, overrides);
        enable(spindle_slider, overrides);
    }
    if (page == JogPage && !actuator_mode) {
        enable(wcs_dropdown, on, brief_busy);
        if (!lv_dropdown_is_open(wcs_dropdown) && state.wcs >= 54 && state.wcs <= 59)
            lv_dropdown_set_selected(wcs_dropdown, state.wcs - 54);
    }
    wifi_refresh_ui();
    int fault = state.alarm ? (state.alarm < 0 ? 1000 : state.alarm) : -state.error;
    if (!fault)
        fault_seen = 0;
    if (fault && fault != fault_seen && !state.recovering) {
        fault_seen = fault;
        recover(nullptr);
    }
    probe_refresh_ui();
    machine_refresh();
    tools_refresh();
    concept_refresh();
}
} // namespace
extern "C" void panel_ui_create() {
    printf("@UGDBG LVGL draw buffer %p\n",lv_display_get_buf_active(lv_display_get_default())->data);
    runtime_init();
    wifi_result(wifi_bridge_init());
    load();
    bsp_display_brightness_set(screen_brightness);
    hardware_init();
    macros_load();
    root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = box(root, 0, 0, 1024, 136, EDGE);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    page_layer = box(root, 0, 0, 1024, 528);
    lv_obj_set_style_bg_opa(page_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page_layer, 0, 0);
    lv_obj_set_style_radius(page_layer, 0, 0);
    lv_obj_remove_flag(page_layer, LV_OBJ_FLAG_CLICKABLE);
    left_surface = surface(root, 8, 64, 488, 440);
    right_surface = surface(root, 500, 64, 516, 440);
    brand_label = label(root, "grblHAL", 16, 20, 148, 22, WHITE);
    wifi_header = button(root, "", 548, 4, 460, 52, show_wifi);
    lv_obj_set_style_bg_color(wifi_header, lv_color_hex(EDGE), 0);
    lv_obj_delete(lv_obj_get_child(wifi_header, 0));
    lv_obj_set_style_bg_grad_dir(wifi_header, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_shadow_width(wifi_header, 0, 0);
    wifi_header_name = label(wifi_header, "Not connected", 16, 4, 380, 18, WHITE);
    lv_obj_set_style_text_align(wifi_header_name, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(wifi_header_name, LV_LABEL_LONG_DOT);
    wifi_header_ip = label(wifi_header, "Wi-Fi", 16, 28, 380, 14, WHITE);
    lv_obj_set_style_text_align(wifi_header_ip, LV_TEXT_ALIGN_RIGHT, 0);
    wifi_icon = label(wifi_header, LV_SYMBOL_WIFI, 412, 12, 40, 28, WHITE);
    state_label = label(root, "Disconnected", 174, 22, 352, 20, MUTED);
    const char *tabs[] = {"Jog", "Files", "Macros", "Diag", "Settings"};
    tabview = lv_tabview_create(root);
    lv_obj_set_pos(tabview, 8, 528);
    lv_obj_set_size(tabview, 1008, 72);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(tabview, 72);
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(CARD), 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);
    lv_obj_set_style_pad_column(tab_bar, 0, 0);
    for (int i = 0; i < 5; i++) {
        lv_tabview_add_tab(tabview, tabs[i]);
        const uint32_t accents[] = {0x176FB5, 0x2D8530, 0x247C87, 0x247C87, 0xA95B12};
        lv_color_t accent = lv_color_hex(accents[i]);
        nav[i] = lv_obj_get_child(tab_bar, i);
        lv_obj_set_style_bg_opa(nav[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(nav[i], LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(nav[i], lv_color_hex(CARD), 0);
        lv_obj_set_style_bg_color(nav[i], accent, LV_STATE_CHECKED);
        lv_obj_set_style_bg_grad_color(nav[i], lv_color_lighten(accent, 24), LV_STATE_CHECKED);
        lv_obj_set_style_bg_grad_dir(nav[i], LV_GRAD_DIR_NONE, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(nav[i], accent, 0);
        lv_obj_set_style_text_color(nav[i], lv_color_hex(WHITE), LV_STATE_CHECKED);
        lv_obj_set_style_text_font(nav[i], font(20), 0);
        lv_obj_set_style_text_font(nav[i], font(22), LV_STATE_CHECKED);
        lv_obj_set_style_radius(nav[i], 0, 0);
        lv_obj_set_style_shadow_width(nav[i], 0, 0);
        lv_obj_set_style_border_width(nav[i], 0, 0);
        lv_obj_set_style_border_width(nav[i], 0, LV_STATE_CHECKED);
    }
    // Page content shares the persistent DRO and is rebuilt by the existing renderer.
    lv_obj_add_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(tabview, navigate, LV_EVENT_VALUE_CHANGED, nullptr);
    const char *axes[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = box(root, 16 + i * 336, 66, 320, 98);
        axis_card[i] = c;
        lv_obj_set_style_border_width(c, 0, 0);
        axis_name[i] = label(c, axes[i], 16, 16, 30, 20, AXIS_COLORS[i]);
        axis_work[i] = label(c, "--.---", 66, 10, 238, 32);
        lv_obj_set_style_text_align(axis_work[i], LV_TEXT_ALIGN_RIGHT, 0);
        axis_machine[i] = label(c, "M  --.---", 104, 65, 110, 14, MUTED);
        lv_obj_set_style_text_align(axis_machine[i], LV_TEXT_ALIGN_RIGHT, 0);
        char s[16];
        snprintf(s, sizeof s, "Home %c", "XYZ"[i]);
        axis_home[i] = button(c, s, 12, 48, 84, 44, home_click, "XYZ"[i], AXIS_COLORS[i]);
        snprintf(s, sizeof s, "Zero %c", "XYZ"[i]);
        axis_zero[i] = button(c, s, 224, 48, 84, 44, zero_click, 1 << i, AXIS_COLORS[i]);
        soften(axis_home[i], AXIS_COLORS[i]);
        soften(axis_zero[i], AXIS_COLORS[i]);
    }
    machine_heading = label(root, "MACHINE", 136, 70, 104, 12, MUTED);
    work_heading = label(root, "WORK", 246, 70, 148, 12, MUTED);
    lv_obj_set_style_text_align(machine_heading, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(work_heading, LV_TEXT_ALIGN_RIGHT, 0);

    dro_actions = box(root, 16, 314, 472, 64, BG);
    lv_obj_set_style_border_width(dro_actions, 0, 0);
    lv_obj_set_style_bg_opa(dro_actions, LV_OPA_TRANSP, 0);
    home_btn = button(dro_actions, "Home all", 0, 0, 112, 64, home_click);
    goto_buttons[0] = button(dro_actions, "Go to XY\nzero", 120, 0, 112, 64, goto_click, 3);
    goto_buttons[1] = button(dro_actions, "Go to Z\nzero", 240, 0, 112, 64, goto_click, 4);
    zero_all_btn = button(dro_actions, "Set zero\nall", 360, 0, 112, 64, zero_click, 7);
    soften(home_btn);
    soften(goto_buttons[0]);
    soften(goto_buttons[1]);
    soften(zero_all_btn);
    jog_tools = box(root, 16, 400, 472, 80, BG);
    lv_obj_set_style_border_width(jog_tools, 0, 0);
    lv_obj_set_style_bg_opa(jog_tools, LV_OPA_TRANSP, 0);
    probe_btn = button(jog_tools, "Probe", 0, 0, 140, 64, show_probe, 0, 0xA95B12);
    accessories_btn = button(jog_tools, "Spindle /\ncoolant", 152, 0, 160, 64, show_accessories);
    wcs_dropdown = dropdown(jog_tools, "G54\nG55\nG56\nG57\nG58\nG59", 0, 324, 8, 148, wcs);
    job_controls = box(root, 16, 330, 228, 80, BG);
    lv_obj_set_style_border_width(job_controls, 0, 0);
    lv_obj_set_style_bg_opa(job_controls, LV_OPA_TRANSP, 0);
    job_pause = button(job_controls, "Pause", 0, 0, 228, 80, job_pause_click, 0, AMBER);
    lv_obj_set_style_text_font(lv_obj_get_child(job_pause, 0), font(22), 0);
    body = surface(root, 16, 240, 992, 264);
    lv_obj_set_style_border_width(body, 0, 0);

    stop_btn = button(root, "Stop", 260, 330, 228, 80, nullptr, 0, RED);
    lv_obj_add_event_cb(stop_btn, stop_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(stop_btn, 0), font(22), 0);
    lv_obj_add_event_cb(state_label, recover, LV_EVENT_CLICKED, nullptr);
    build_page();
    runtime_connect((int)prefs.baud, usb_connection);
    lv_timer_create(refresh, 100, nullptr);
}
extern "C" void panel_ui_settings(char *buffer, size_t size) {
    const char *dialog = probe_offsets_open ? "probe-offsets" : number ? "number-keypad" :
                         server_open ? "preview-server" : machine_editor ? "machine-editor" :
                         machine_browser ? "machine-settings" : modal ? "other" : "none";
    const char *form_kind = probe_offsets_open ? "probe-configuration" : number ? "numeric" : server_open ? "preview-server" :
        wifi_password ? "wifi-password" : wifi_list ? "wifi-scan" : spindles_open ? "spindles" : accessories_open ? "tools" :
        macro_editor_open ? "macro-editor" : actuator_config_open ? "actuator-editor" : machine_editor ? "machine-editor" :
        machine_search ? "machine-search" : machine_browser ? "machine-settings" : probe_status ? "probe" :
        preview_update_open ? "preview-update" : confirmation ? "confirmation" : modal ? "notice" : "none";
    unsigned macro_count = 0;
    for (const auto &m : macros->entries) if (*m.name) ++macro_count;
    snprintf(buffer, size,
             "{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"diameter\":%.3f,\"feed\":%.3f,\"inset\":%.3f,\"tx\":%.0f,\"rx\":%.0f,"
             "\"baud\":%.0f,\"usb_connection\":%d,\"inches\":%d,\"step\":%d,\"step_mm\":%.3f,\"page\":%d,\"actuator_mode\":%d,\"a_rotary\":%d,\"macro_count\":%u,\"preview_view\":%u,\"local_preview\":%d,\"single_probe\":%d,\"probe_surface\":%u,\"brightness\":%u,\"rgb_ready\":%d,\"rgb_color\":%lu,\"can_setup\":%d,\"probe_open\":%d,\"continuous\":%d,\"mode_checked\":%d,\"jog_speed\":%u,\"jog_hold_ms\":%lu,\"jog_control_updates\":%lu,\"jog_enabled\":%d,\"scroll_y\":%d,\"dialog\":\"%s\",\"number_value\":%.6f,\"probe_edit_x\":%.6f,\"probe_edit_y\":%.6f,\"probe_edit_z\":%.6f,\"probe_edit_feed\":%.6f,\"probe_edit_inset\":%.6f,\"header_x\":%d,\"header_y\":%d,\"header_w\":%d,\"header_h\":%d,\"form_w\":%d,\"form_h\":%d,\"form_kind\":\"%s\"}",
             prefs.probe.x, prefs.probe.y, prefs.probe.z, prefs.probe.diameter, prefs.probe.feed, prefs.probe.inset, prefs.tx,
             prefs.rx, prefs.baud, usb_connection, inches, step_index, jog_step_mm(inches, step_index), page, actuator_mode, a_rotary, macro_count, cached_view, local_preview_show, single_probe, probe_surface_choice, screen_brightness, hardware_led_ready(), (unsigned long)hardware_led_color(), ready(), probe_start != nullptr, continuous,
             page == JogPage && !actuator_mode && mode_choices
                 ? (lv_buttonmatrix_has_button_ctrl(mode_choices, 1, LV_BUTTONMATRIX_CTRL_CHECKED) ? 1
                    : lv_buttonmatrix_has_button_ctrl(mode_choices, 0, LV_BUTTONMATRIX_CTRL_CHECKED) ? 0 : -1)
                 : -1, jog_speed, (unsigned long)jog_hold_ms, (unsigned long)jog_control_updates, jog_count && !lv_obj_has_state(jog_buttons[0], LV_STATE_DISABLED), body ? lv_obj_get_scroll_y(body) : 0, dialog,
             number ? strtof(lv_textarea_get_text(number),nullptr) : 0.0,
             probe_offsets_open ? strtof(lv_textarea_get_text(probe_offset_fields[0]),nullptr) : 0.0,
             probe_offsets_open ? strtof(lv_textarea_get_text(probe_offset_fields[1]),nullptr) : 0.0,
             probe_offsets_open ? strtof(lv_textarea_get_text(probe_offset_fields[2]),nullptr) : 0.0,
             probe_offsets_open ? strtof(lv_textarea_get_text(probe_offset_fields[3]),nullptr) : 0.0,
             probe_offsets_open ? strtof(lv_textarea_get_text(probe_offset_fields[4]),nullptr) : 0.0,
             lv_obj_get_x(wifi_header),lv_obj_get_y(wifi_header),lv_obj_get_width(wifi_header),lv_obj_get_height(wifi_header),
             modal?lv_obj_get_width(lv_obj_get_child(modal,0)):0,modal?lv_obj_get_height(lv_obj_get_child(modal,0)):0,form_kind);
}
extern "C" bool panel_ui_setting_field(const char *text) {
    if (probe_offsets_open && text && strlen(text) <= 10) {
        lv_textarea_set_text(lv_keyboard_get_textarea(probe_offsets_keyboard),text);
        return true;
    }
    if ((!machine_editor && !machine_search) || !machine_field || !text || strlen(text) > MaxSettingValue)
        return false;
    lv_textarea_set_text(machine_field, text);
    return true;
}
extern "C" bool panel_ui_macro_field(unsigned index, const char *text) {
    if (!macro_editor_open || index > 1 || !text || strlen(text) > (index ? MaxMacroCode : 32)) return false;
    lv_textarea_set_text(macro_fields[index], text);
    return true;
}

extern "C" bool panel_ui_wifi_password(const char *password) {
    if (!wifi_password || !password || strlen(password) > 64)
        return false;
    lv_textarea_set_text(wifi_password, password);
    return true;
}
extern "C" bool panel_ui_actuator_field(unsigned index, const char *text) {
    if (!actuator_config_open || index >= 6 || !text || strlen(text) > 48)
        return false;
    lv_textarea_set_text(act_fields[index], text);
    return true;
}
extern "C" bool panel_ui_actuator_json(char *buffer, size_t size) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return false;
    cJSON_AddBoolToObject(j, "actuator_mode", actuator_mode);
    cJSON_AddBoolToObject(j, "online", state.online && state.status);
    cJSON_AddStringToObject(j, "state", state.mode);
    cJSON_AddBoolToObject(j, "preview", runtime_is_demo());
    cJSON_AddNumberToObject(j, "alarm", state.alarm);
    cJSON_AddNumberToObject(j, "error", state.error);
    cJSON_AddNumberToObject(j, "repeat_axis", state.cycle_axis);
    cJSON *list = cJSON_AddArrayToObject(j, "actuators");
    for (int i = 0; i < 4; i++) {
        const auto &a = actuator_prefs.axes[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToArray(list, item);
        char axis[] = {"XYZA"[i], 0};
        cJSON_AddStringToObject(item, "axis", axis);
        cJSON_AddStringToObject(item, "name", a.name);
        cJSON_AddStringToObject(item, "first_label", a.first);
        cJSON_AddStringToObject(item, "second_label", a.second);
        cJSON_AddNumberToObject(item, "style", a.style);
        cJSON_AddBoolToObject(item, "repeating", state.cycle_axis == i);
        bool rotary = i == 3 && a_rotary;
        cJSON_AddBoolToObject(item, "rotary", rotary);
        cJSON_AddStringToObject(item, "unit", rotary ? "deg" : "mm");
        cJSON_AddNumberToObject(item, "first", a.end1);
        cJSON_AddNumberToObject(item, "second", a.end2);
        cJSON_AddNumberToObject(item, "speed", a.feed);
        if (!rotary) {
            cJSON_AddNumberToObject(item, "first_mm", a.end1);
            cJSON_AddNumberToObject(item, "second_mm", a.end2);
            cJSON_AddNumberToObject(item, "speed_mm_min", a.feed);
        }
        cJSON_AddBoolToObject(item, "available", unsigned(i) < state.axis_count);
        bool known = state.online && state.status && state.machine_known &&
                     (i != 3 || (state.rotary_known && rotary == bool(state.rotary_axes & 8))) &&
                     unsigned(i) < state.axis_count && (state.homed_axes & (1u << i));
        cJSON_AddBoolToObject(item, "homed", known);
        if (known) {
            cJSON_AddNumberToObject(item, "position", state.machine[i]);
            if (!rotary)
                cJSON_AddNumberToObject(item, "position_mm", state.machine[i]);
        } else {
            cJSON_AddNullToObject(item, "position");
            if (!rotary)
                cJSON_AddNullToObject(item, "position_mm");
        }
        if (known && a.end1 != a.end2)
            cJSON_AddNumberToObject(item, "position_percent", fminf(100, fmaxf(0,
                (state.machine[i] - a.end1) * 100 / (a.end2 - a.end1))));
        else
            cJSON_AddNullToObject(item, "position_percent");
    }
    bool ok = cJSON_PrintPreallocated(j, buffer, size, false);
    cJSON_Delete(j);
    return ok;
}

extern "C" bool panel_ui_preview_server_field(const char *text) {
    if(!server_open||!server_field||!text||strlen(text)>127)return false;
    lv_textarea_set_text(server_field,text);return true;
}
