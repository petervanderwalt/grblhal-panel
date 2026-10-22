#include "cnc_wizard.h"
#include "cnc_wizard_theme.h"

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "cnc_link.h"
#include "gcode_viewer.h"
#include "wifi_bridge.h"
#include "nvs_flash.h"
#include "nvs.h"


#define MAX_GCODE_FILES 48
#define MAX_SELECTED_FILE 256
#define NUM_TOOLS 12
#define TOP_BAR_H 40
#define BOTTOM_BAR_H 64

static void event_back_to_splash(lv_event_t *e);
static void event_back_to_tool(lv_event_t *e);
static void event_file_next(lv_event_t *e);
static void event_settings_open(lv_event_t *e);
static void event_password_focused(lv_event_t *e);
static void event_jog_diag(lv_event_t *e);
static void event_jog_stop(lv_event_t *e);
static lv_obj_t *create_jog_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t size, const char *label, lv_event_cb_t cb, void *udata, lv_color_t color);

typedef enum {
    WIZARD_SPLASH,
    WIZARD_HOME,
    WIZARD_FILE_SELECT,
    WIZARD_ORIGIN_CHOICE,
    WIZARD_JOG,
    WIZARD_PROBE,
    WIZARD_TOOL_SELECT,
    WIZARD_READY,
    WIZARD_RUNNING,
    WIZARD_COMPLETE,
    WIZARD_MANUAL,
    WIZARD_TOOLS,
    WIZARD_SETUP,
} wizard_state_t;

enum {
    JOG_ACTION_NONE = 0,
    JOG_ACTION_X_MINUS,
    JOG_ACTION_X_PLUS,
    JOG_ACTION_Y_PLUS,
    JOG_ACTION_Y_MINUS,
    JOG_ACTION_Z_PLUS,
    JOG_ACTION_Z_MINUS,
    JOG_ACTION_A_PLUS,
    JOG_ACTION_A_MINUS,
};

typedef struct {
    wizard_state_t state;
    lv_display_t *disp;
    lv_obj_t *screen;
    lv_obj_t *top_bar;
    lv_obj_t *content;
    lv_obj_t *bottom_bar;
    lv_obj_t *bottom_back_btn;
    lv_obj_t *bottom_next_btn;
    lv_obj_t *bottom_next_label;

    char selected_file[MAX_SELECTED_FILE];
    int selected_tool;
    char tool_names[NUM_TOOLS][48];
    float origin_x;
    float origin_y;
    float origin_z;
    bool origin_set;

    int feed_override_pct;
    int speed_override_pct;

    lv_timer_t *refresh_timer;
    lv_timer_t *jog_repeat_timer;
    lv_timer_t *sim_timer;
    lv_timer_t *stream_timer;
    lv_timer_t *wifi_scan_poll_timer;
    FILE *stream_file;
    int stream_lines_sent;
    bool paused;
    int sim_step;
    int jog_repeat_action;

    lv_obj_t *status_dot;
    lv_obj_t *conn_label;
    lv_obj_t *dro_x;
    lv_obj_t *dro_y;
    lv_obj_t *dro_z;
    lv_obj_t *settings_screen;
    lv_obj_t *settings_modal;
    lv_obj_t *modal_wifi_label;
    lv_obj_t *modal_wifi_scan_label;
    lv_obj_t *modal_wifi_dropdown;
    lv_obj_t *modal_ssid_ta;
    lv_obj_t *modal_password_ta;
    lv_obj_t *modal_keyboard;
    char modal_wifi_options[512];
    lv_obj_t *file_list;
    lv_obj_t *tool_dropdown;
    lv_obj_t *next_disabled_flag;
    char s_file_paths[MAX_GCODE_FILES][192];
    size_t s_file_count;

    lv_obj_t *viewer_container;
    lv_obj_t *job_progress_bar;
    lv_obj_t *job_progress_label;
    lv_obj_t *feed_label;
    lv_obj_t *speed_label;

    int spindle_speed;
    lv_obj_t *spindle_kb;
    lv_obj_t *spindle_speed_ta;
    lv_obj_t *spindle_speed_display;
    lv_obj_t *spindle_speed_title;
    lv_obj_t *spindle_modal_bg;
    lv_obj_t *target_kb;
    lv_obj_t *target_ta;
    lv_obj_t *target_title;
    lv_obj_t *target_modal_bg;
    char target_axis;
    float target_x, target_y, target_z, target_a;
    lv_obj_t *man_dro_x;
    lv_obj_t *man_dro_y;
    lv_obj_t *man_dro_z;
    lv_obj_t *man_dro_a;
    bool wizard_flow;
} cnc_wizard_t;

static cnc_wizard_t s_wiz;

#define NVS_NS "ug_sender"

static void persist_save_int(const char *key, int32_t val)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, key, val);
        nvs_commit(h);
        nvs_close(h);
    }
}

static int32_t persist_load_int(const char *key, int32_t default_val)
{
    nvs_handle_t h;
    int32_t val = default_val;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, key, &val);
        nvs_close(h);
    }
    return val;
}

static void persist_save_str(const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, key, val ? val : "");
        nvs_commit(h);
        nvs_close(h);
    }
}

static void persist_load_str(const char *key, char *buf, size_t len, const char *default_val)
{
    nvs_handle_t h;
    size_t sz = len;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, key, buf, &sz) != ESP_OK) {
            strlcpy(buf, default_val ? default_val : "", len);
        }
        nvs_close(h);
    } else {
        strlcpy(buf, default_val ? default_val : "", len);
    }
}

static void persist_load_and_apply(void)
{
    s_wiz.selected_tool = (int)persist_load_int("tool", 0);
    s_wiz.feed_override_pct = (int)persist_load_int("feed", 100);
    s_wiz.speed_override_pct = (int)persist_load_int("speed", 100);
    s_wiz.spindle_speed = (int)persist_load_int("spindle", 12000);
}

static void set_label_textf(lv_obj_t *label, const char *fmt, ...)
{
    static char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    lv_label_set_text(label, buffer);
}

static const char *path_basename(const char *path)
{
    const char *f = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *base = f;
    if (!base || (b && b > base)) base = b;
    return base ? base + 1 : path;
}

static bool has_supported_extension(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return strcasecmp(ext, ".gcode") == 0 ||
           strcasecmp(ext, ".nc") == 0 ||
           strcasecmp(ext, ".gc") == 0 ||
           strcasecmp(ext, ".tap") == 0;
}

static void navigate_to(wizard_state_t state);
static void clear_content(void);
static void build_top_bar(void);
static void build_bottom_bar(const char *back_label, lv_event_cb_t back_cb, const char *next_label, lv_event_cb_t next_cb, bool next_enabled);

static void network_refresh_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (cnc_link_is_connected()) {
        cnc_link_request_status();
        cnc_link_snapshot_t snap;
        cnc_link_get_snapshot(&snap);
        if (snap.has_status) {
            set_label_textf(s_wiz.dro_x, "X:%.1f", snap.x);
            set_label_textf(s_wiz.dro_y, "Y:%.1f", snap.y);
            set_label_textf(s_wiz.dro_z, "Z:%.1f", snap.z);
        }
        const char *state = snap.machine_state[0] ? snap.machine_state : "Idle";
        lv_label_set_text(s_wiz.conn_label, state);
        lv_obj_set_style_text_color(s_wiz.conn_label, WIZARD_COLOR_GREEN_ACCENT, 0);
        lv_obj_set_style_bg_color(s_wiz.status_dot, WIZARD_COLOR_GREEN_ACCENT, 0);
    } else {
        lv_label_set_text(s_wiz.conn_label, "Disconnected");
        lv_obj_set_style_text_color(s_wiz.conn_label, WIZARD_COLOR_RED_ACCENT, 0);
        lv_obj_set_style_bg_color(s_wiz.status_dot, WIZARD_COLOR_RED_ACCENT, 0);
    }
}

static void event_unlock(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_unlock();
}

static void build_top_bar(void)
{
    s_wiz.top_bar = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.top_bar, lv_pct(100), TOP_BAR_H);
    lv_obj_set_style_bg_color(s_wiz.top_bar, WIZARD_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(s_wiz.top_bar, 0, 1);
    lv_obj_set_style_border_color(s_wiz.top_bar, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_pad_all(s_wiz.top_bar, 0, 0);
    lv_obj_clear_flag(s_wiz.top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.top_bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(s_wiz.top_bar);
    lv_label_set_text(title, "uGsender");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    s_wiz.status_dot = lv_obj_create(s_wiz.top_bar);
    lv_obj_set_size(s_wiz.status_dot, 10, 10);
    lv_obj_set_style_bg_color(s_wiz.status_dot, WIZARD_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_border_width(s_wiz.status_dot, 0, 0);
    lv_obj_set_style_radius(s_wiz.status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(s_wiz.status_dot, LV_ALIGN_RIGHT_MID, -10, 0);

    s_wiz.conn_label = lv_label_create(s_wiz.top_bar);
    lv_label_set_text(s_wiz.conn_label, "Disconnected");
    lv_obj_set_style_text_color(s_wiz.conn_label, WIZARD_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_text_font(s_wiz.conn_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_wiz.conn_label, LV_ALIGN_RIGHT_MID, -70, 0);

    lv_obj_t *dro_cont = lv_obj_create(s_wiz.top_bar);
    lv_obj_set_size(dro_cont, 300, TOP_BAR_H);
    lv_obj_set_style_bg_opa(dro_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dro_cont, 0, 0);
    lv_obj_clear_flag(dro_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dro_cont, LV_ALIGN_CENTER, 60, 0);

    s_wiz.dro_x = lv_label_create(dro_cont);
    lv_label_set_text(s_wiz.dro_x, "X:---");
    lv_obj_set_style_text_color(s_wiz.dro_x, WIZARD_COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_wiz.dro_x, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wiz.dro_x, LV_ALIGN_LEFT_MID, 0, 0);

    s_wiz.dro_y = lv_label_create(dro_cont);
    lv_label_set_text(s_wiz.dro_y, "Y:---");
    lv_obj_set_style_text_color(s_wiz.dro_y, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_set_style_text_font(s_wiz.dro_y, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wiz.dro_y, LV_ALIGN_LEFT_MID, 90, 0);

    s_wiz.dro_z = lv_label_create(dro_cont);
    lv_label_set_text(s_wiz.dro_z, "Z:---");
    lv_obj_set_style_text_color(s_wiz.dro_z, WIZARD_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(s_wiz.dro_z, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wiz.dro_z, LV_ALIGN_LEFT_MID, 180, 0);
}

static void build_bottom_bar(const char *back_label, lv_event_cb_t back_cb, const char *next_label, lv_event_cb_t next_cb, bool next_enabled)
{
    if (s_wiz.bottom_bar) {
        lv_obj_del(s_wiz.bottom_bar);
        s_wiz.bottom_bar = NULL;
        s_wiz.bottom_back_btn = NULL;
        s_wiz.bottom_next_btn = NULL;
        s_wiz.bottom_next_label = NULL;
    }

    s_wiz.bottom_bar = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.bottom_bar, lv_pct(100), BOTTOM_BAR_H);
    lv_obj_set_style_bg_color(s_wiz.bottom_bar, WIZARD_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(s_wiz.bottom_bar, 0, 1);
    lv_obj_set_style_border_color(s_wiz.bottom_bar, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_pad_all(s_wiz.bottom_bar, 0, 0);
    lv_obj_clear_flag(s_wiz.bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    if (back_label) {
        s_wiz.bottom_back_btn = lv_button_create(s_wiz.bottom_bar);
        lv_obj_set_size(s_wiz.bottom_back_btn, 120, 44);
        lv_obj_add_style(s_wiz.bottom_back_btn, wizard_get_danger_btn_style(), 0);
        lv_obj_align(s_wiz.bottom_back_btn, LV_ALIGN_LEFT_MID, 16, 0);
        if (back_cb) {
            lv_obj_add_event_cb(s_wiz.bottom_back_btn, back_cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *bl = lv_label_create(s_wiz.bottom_back_btn);
        lv_label_set_text(bl, back_label);
        lv_obj_center(bl);
    }

    if (next_label) {
        s_wiz.bottom_next_btn = lv_button_create(s_wiz.bottom_bar);
        lv_obj_set_size(s_wiz.bottom_next_btn, 180, 48);
        lv_obj_set_style_radius(s_wiz.bottom_next_btn, 8, 0);
        lv_obj_set_style_border_width(s_wiz.bottom_next_btn, 0, 0);
        lv_obj_align(s_wiz.bottom_next_btn, LV_ALIGN_RIGHT_MID, -16, 0);
        if (next_enabled) {
            lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_BLUE_ACCENT, 0);
            lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, lv_color_darken(WIZARD_COLOR_BLUE_ACCENT, 30), LV_STATE_PRESSED);
        } else {
            lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_DIM_GRAY, 0);
            lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_DIM_GRAY, LV_STATE_PRESSED);
            lv_obj_add_state(s_wiz.bottom_next_btn, LV_STATE_DISABLED);
        }
        if (next_cb) {
            lv_obj_add_event_cb(s_wiz.bottom_next_btn, next_cb, LV_EVENT_CLICKED, NULL);
        }
        s_wiz.bottom_next_label = lv_label_create(s_wiz.bottom_next_btn);
        lv_label_set_text(s_wiz.bottom_next_label, next_label);
        lv_obj_set_style_text_font(s_wiz.bottom_next_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(s_wiz.bottom_next_label, WIZARD_COLOR_WHITE, 0);
        lv_obj_center(s_wiz.bottom_next_label);
    }
}

static void clear_content(void)
{
    if (s_wiz.sim_timer) {
        lv_timer_del(s_wiz.sim_timer);
        s_wiz.sim_timer = NULL;
    }
    if (s_wiz.stream_timer) {
        lv_timer_delete(s_wiz.stream_timer);
        s_wiz.stream_timer = NULL;
    }
    if (s_wiz.stream_file) {
        fclose(s_wiz.stream_file);
        s_wiz.stream_file = NULL;
    }
    s_wiz.stream_lines_sent = 0;
    s_wiz.paused = false;
    if (s_wiz.spindle_modal_bg) {
        lv_obj_add_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wiz.spindle_kb) {
        lv_obj_add_flag(s_wiz.spindle_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wiz.spindle_speed_title) {
        lv_obj_add_flag(s_wiz.spindle_speed_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wiz.wifi_scan_poll_timer) {
        lv_timer_del(s_wiz.wifi_scan_poll_timer);
        s_wiz.wifi_scan_poll_timer = NULL;
    }
    if (s_wiz.content) {
        gcode_viewer_detach();
        lv_obj_del(s_wiz.content);
        s_wiz.content = NULL;
    }
    if (s_wiz.bottom_bar) {
        lv_obj_del(s_wiz.bottom_bar);
        s_wiz.bottom_bar = NULL;
        s_wiz.bottom_back_btn = NULL;
        s_wiz.bottom_next_btn = NULL;
        s_wiz.bottom_next_label = NULL;
    }
    s_wiz.job_progress_bar = NULL;
    s_wiz.job_progress_label = NULL;
}

/* ─── SPLASH ─── */
static void event_splash_done(lv_anim_t *a)
{
    LV_UNUSED(a);
    navigate_to(WIZARD_HOME);
}

static void anim_text_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void build_splash(void)
{
    clear_content();
    s_wiz.state = WIZARD_SPLASH;

    lv_obj_t *cont = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont, WIZARD_COLOR_DARK_BG, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, "Sienci Labs");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(label, WIZARD_COLOR_TEXT, 0);
    lv_obj_center(label);

    lv_obj_t *sub = lv_label_create(cont);
    lv_label_set_text(sub, "uGsender CNC");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 40);

    s_wiz.content = cont;
    lv_refr_now(s_wiz.disp);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, anim_text_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_ready_cb(&a, event_splash_done);
    lv_anim_start(&a);
}

/* ─── FILE SELECT ─── */
static void file_load_progress_cb(int progress, void *user_data)
{
    LV_UNUSED(user_data);
    if (s_wiz.job_progress_bar) {
        lv_bar_set_value(s_wiz.job_progress_bar, progress, LV_ANIM_OFF);
        lv_refr_now(s_wiz.disp);
    }
}

/* ─── SETTINGS MODAL ─── */
static void event_close_modal(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_wiz.settings_screen) {
        if (s_wiz.modal_keyboard) {
            lv_obj_add_flag(s_wiz.modal_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        lv_screen_load(s_wiz.screen);
    }
}

static void event_password_focused(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        if (s_wiz.modal_keyboard) {
            lv_obj_add_flag(s_wiz.modal_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (s_wiz.modal_keyboard) {
        lv_obj_clear_flag(s_wiz.modal_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_wiz.modal_keyboard, s_wiz.modal_password_ta);
    }
}

static void refresh_modal_wifi_label(void)
{
    char buf[160];
    wifi_bridge_get_status(buf, sizeof(buf));
    lv_label_set_text(s_wiz.modal_wifi_label, buf);
}

static void refresh_modal_wifi_dropdown(void)
{
    wifi_bridge_scan_result_t results[WIFI_BRIDGE_MAX_SCAN_RESULTS];
    size_t count = wifi_bridge_get_scan_results(results, WIFI_BRIDGE_MAX_SCAN_RESULTS);
    if (count == 0) {
        strlcpy(s_wiz.modal_wifi_options, "No networks", sizeof(s_wiz.modal_wifi_options));
        lv_dropdown_set_options(s_wiz.modal_wifi_dropdown, s_wiz.modal_wifi_options);
        return;
    }
    s_wiz.modal_wifi_options[0] = '\0';
    size_t offset = 0;
    for (size_t i = 0; i < count && offset < sizeof(s_wiz.modal_wifi_options) - 2; ++i) {
        offset += snprintf(s_wiz.modal_wifi_options + offset, sizeof(s_wiz.modal_wifi_options) - offset,
                           "%s\n", results[i].ssid);
    }
    size_t len = strlen(s_wiz.modal_wifi_options);
    if (len > 0 && s_wiz.modal_wifi_options[len - 1] == '\n') {
        s_wiz.modal_wifi_options[len - 1] = '\0';
    }
    lv_dropdown_set_options(s_wiz.modal_wifi_dropdown, s_wiz.modal_wifi_options);
}

static void event_wifi_connect(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *ssid = lv_textarea_get_text(s_wiz.modal_ssid_ta);
    const char *pass = lv_textarea_get_text(s_wiz.modal_password_ta);
    persist_save_str("wifi_ssid", ssid);
    persist_save_str("wifi_pass", pass);
    wifi_bridge_connect(ssid, pass);
    refresh_modal_wifi_label();
}

static void wifi_scan_poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!wifi_bridge_is_scanning()) {
        refresh_modal_wifi_dropdown();
        refresh_modal_wifi_label();
        if (s_wiz.wifi_scan_poll_timer) {
            lv_timer_del(s_wiz.wifi_scan_poll_timer);
            s_wiz.wifi_scan_poll_timer = NULL;
        }
        if (s_wiz.modal_wifi_scan_label) {
            lv_label_set_text(s_wiz.modal_wifi_scan_label, "Scan complete");
            lv_obj_set_style_text_color(s_wiz.modal_wifi_scan_label, WIZARD_COLOR_GREEN_ACCENT, 0);
        }
    }
}

static void event_wifi_scan(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_wiz.modal_wifi_scan_label) {
        lv_label_set_text(s_wiz.modal_wifi_scan_label, "Scanning...");
        lv_obj_set_style_text_color(s_wiz.modal_wifi_scan_label, WIZARD_COLOR_BLUE_ACCENT, 0);
        lv_obj_clear_flag(s_wiz.modal_wifi_scan_label, LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(s_wiz.disp);
    }
    lv_dropdown_set_options(s_wiz.modal_wifi_dropdown, "Scanning...");
    wifi_bridge_scan();
    if (!s_wiz.wifi_scan_poll_timer) {
        s_wiz.wifi_scan_poll_timer = lv_timer_create(wifi_scan_poll_cb, 200, NULL);
    }
}

static void event_wifi_dropdown_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    uint16_t index = lv_dropdown_get_selected(s_wiz.modal_wifi_dropdown);
    wifi_bridge_scan_result_t results[WIFI_BRIDGE_MAX_SCAN_RESULTS];
    size_t count = wifi_bridge_get_scan_results(results, WIFI_BRIDGE_MAX_SCAN_RESULTS);
    if (index < count && results[index].ssid[0]) {
        lv_textarea_set_text(s_wiz.modal_ssid_ta, results[index].ssid);
    }
}

static void style_modal_input(lv_obj_t *obj, lv_coord_t height)
{
    lv_obj_set_height(obj, height);
    lv_obj_set_style_bg_color(obj, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_text_color(obj, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_pad_left(obj, 10, 0);
    lv_obj_set_style_pad_right(obj, 10, 0);
    lv_obj_set_style_pad_top(obj, 4, 0);
    lv_obj_set_style_pad_bottom(obj, 4, 0);
}

static void build_settings_modal(void)
{
    if (s_wiz.settings_screen) {
        refresh_modal_wifi_label();
        refresh_modal_wifi_dropdown();
        if (s_wiz.modal_keyboard) {
            lv_obj_add_flag(s_wiz.modal_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        lv_screen_load(s_wiz.settings_screen);
        return;
    }

    s_wiz.settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wiz.settings_screen, WIZARD_COLOR_DARK_BG, 0);
    lv_obj_clear_flag(s_wiz.settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(s_wiz.settings_screen);
    lv_obj_set_size(header, lv_pct(100), TOP_BAR_H);
    lv_obj_set_style_bg_color(header, WIZARD_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(header, 0, 1);
    lv_obj_set_style_border_color(header, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(close_btn, event_close_modal, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cl, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_center(cl);

    lv_obj_t *scroll = lv_obj_create(s_wiz.settings_screen);
    lv_obj_set_size(scroll, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 16, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_add_style(scroll, wizard_get_scrollbar_style(), 0);

    s_wiz.modal_wifi_label = lv_label_create(scroll);
    lv_obj_set_width(s_wiz.modal_wifi_label, lv_pct(100));
    lv_label_set_long_mode(s_wiz.modal_wifi_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_wiz.modal_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_wiz.modal_wifi_label, WIZARD_COLOR_GRAY, 0);

    s_wiz.modal_wifi_scan_label = lv_label_create(scroll);
    lv_obj_set_width(s_wiz.modal_wifi_scan_label, lv_pct(100));
    lv_obj_set_style_text_font(s_wiz.modal_wifi_scan_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_wiz.modal_wifi_scan_label, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_add_flag(s_wiz.modal_wifi_scan_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl = lv_label_create(scroll);
    lv_label_set_text(lbl, "Networks");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, WIZARD_COLOR_GRAY, 0);

    s_wiz.modal_wifi_dropdown = lv_dropdown_create(scroll);
    lv_dropdown_set_options(s_wiz.modal_wifi_dropdown, "No networks");
    style_modal_input(s_wiz.modal_wifi_dropdown, 30);
    lv_obj_set_width(s_wiz.modal_wifi_dropdown, lv_pct(100));
    lv_obj_add_event_cb(s_wiz.modal_wifi_dropdown, event_wifi_dropdown_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *scan_btn = wizard_create_accent_btn(scroll, "Scan", event_wifi_scan);
    lv_obj_set_width(scan_btn, lv_pct(100));

    lbl = lv_label_create(scroll);
    lv_label_set_text(lbl, "SSID");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, WIZARD_COLOR_GRAY, 0);

    s_wiz.modal_ssid_ta = lv_textarea_create(scroll);
    lv_textarea_set_placeholder_text(s_wiz.modal_ssid_ta, "SSID");
    lv_textarea_set_one_line(s_wiz.modal_ssid_ta, true);
    style_modal_input(s_wiz.modal_ssid_ta, 30);
    lv_obj_set_width(s_wiz.modal_ssid_ta, lv_pct(100));

    lbl = lv_label_create(scroll);
    lv_label_set_text(lbl, "Password");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, WIZARD_COLOR_GRAY, 0);

    s_wiz.modal_password_ta = lv_textarea_create(scroll);
    lv_textarea_set_placeholder_text(s_wiz.modal_password_ta, "Password");
    lv_textarea_set_password_mode(s_wiz.modal_password_ta, true);
    lv_textarea_set_one_line(s_wiz.modal_password_ta, true);
    style_modal_input(s_wiz.modal_password_ta, 30);
    lv_obj_set_width(s_wiz.modal_password_ta, lv_pct(100));
    lv_obj_add_event_cb(s_wiz.modal_password_ta, event_password_focused, LV_EVENT_FOCUSED, NULL);

    char saved_ssid[64], saved_pass[64];
    persist_load_str("wifi_ssid", saved_ssid, sizeof(saved_ssid), "");
    persist_load_str("wifi_pass", saved_pass, sizeof(saved_pass), "");
    if (saved_ssid[0]) lv_textarea_set_text(s_wiz.modal_ssid_ta, saved_ssid);
    if (saved_pass[0]) lv_textarea_set_text(s_wiz.modal_password_ta, saved_pass);

    lv_obj_t *conn_btn = wizard_create_accent_btn(scroll, "Connect", event_wifi_connect);
    lv_obj_set_width(conn_btn, lv_pct(100));

    lv_obj_t *kb = lv_keyboard_create(scroll);
    lv_obj_set_size(kb, lv_pct(100), 200);
    lv_obj_set_style_bg_color(kb, WIZARD_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_color(kb, WIZARD_COLOR_INPUT_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, WIZARD_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, WIZARD_COLOR_LIGHT_GRAY, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, event_password_focused, LV_EVENT_READY, NULL);
    s_wiz.modal_keyboard = kb;

    refresh_modal_wifi_label();
    refresh_modal_wifi_dropdown();
    lv_screen_load(s_wiz.settings_screen);
}

static void event_settings_open(lv_event_t *e)
{
    LV_UNUSED(e);
    build_settings_modal();
}

static void event_file_selected(lv_event_t *e)
{
    intptr_t index = (intptr_t)lv_event_get_user_data(e);
    if (index < 0 || (size_t)index >= s_wiz.s_file_count) return;

    strlcpy(s_wiz.selected_file, s_wiz.s_file_paths[index], MAX_SELECTED_FILE);
    lv_obj_t *viewer = s_wiz.viewer_container;
    if (!viewer) {
        clear_content();
        navigate_to(WIZARD_FILE_SELECT);
        return;
    }

    char status[128];
    lv_obj_clear_flag(s_wiz.job_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_wiz.job_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(s_wiz.job_progress_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_wiz.job_progress_label, "Loading...");
    gcode_viewer_set_progress_callback(file_load_progress_cb, NULL);
    lv_refr_now(s_wiz.disp);
    gcode_viewer_load_file(s_wiz.selected_file, status, sizeof(status));
    lv_label_set_text(s_wiz.job_progress_label, "Select a file to preview");

    build_bottom_bar("Close", event_back_to_splash, "Next", event_file_next, true);
    lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_BLUE_ACCENT, 0);
    lv_obj_clear_state(s_wiz.bottom_next_btn, LV_STATE_DISABLED);
    lv_obj_t *existing = lv_obj_get_child(s_wiz.bottom_next_btn, 0);
    if (existing && lv_obj_check_type(existing, &lv_label_class)) {
        lv_label_set_text(existing, "Next");
    }
}

static void event_file_next(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_ORIGIN_CHOICE);
}

static void event_refresh_files(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_FILE_SELECT);
}

static void build_file_select(void)
{
    clear_content();
    s_wiz.state = WIZARD_FILE_SELECT;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Select GCODE File");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *main_row = lv_obj_create(s_wiz.content);
    lv_obj_set_size(main_row, lv_pct(100), 420);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    lv_obj_clear_flag(main_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(main_row, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *left_panel = lv_obj_create(main_row);
    lv_obj_set_size(left_panel, 340, 420);
    lv_obj_add_style(left_panel, wizard_get_card_style(), 0);
    lv_obj_align(left_panel, LV_ALIGN_LEFT_MID, 10, 0);

    s_wiz.file_list = lv_list_create(left_panel);
    lv_obj_set_size(s_wiz.file_list, lv_pct(100), 340);
    lv_obj_add_style(s_wiz.file_list, wizard_get_scrollbar_style(), 0);
    lv_obj_set_style_bg_opa(s_wiz.file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wiz.file_list, 0, 0);
    lv_obj_set_style_pad_all(s_wiz.file_list, 4, 0);
    lv_obj_align(s_wiz.file_list, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *refresh_btn = lv_button_create(left_panel);
    lv_obj_set_size(refresh_btn, 140, 36);
    lv_obj_set_style_bg_color(refresh_btn, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_set_style_border_width(refresh_btn, 1, 0);
    lv_obj_set_style_border_color(refresh_btn, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(refresh_btn, event_refresh_files, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(refresh_btn);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_color(rl, WIZARD_COLOR_TEXT, 0);
    lv_obj_center(rl);

    lv_obj_t *right_panel = lv_obj_create(main_row);
    lv_obj_set_size(right_panel, 640, 420);
    lv_obj_add_style(right_panel, wizard_get_card_style(), 0);
    lv_obj_align(right_panel, LV_ALIGN_LEFT_MID, 370, 0);
    lv_obj_set_style_clip_corner(right_panel, true, 0);

    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_wiz.viewer_container = right_panel;

    gcode_viewer_attach(right_panel, 640, 420);

    s_wiz.job_progress_bar = lv_bar_create(right_panel);
    lv_obj_set_size(s_wiz.job_progress_bar, 300, 8);
    lv_obj_align(s_wiz.job_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_bar_set_range(s_wiz.job_progress_bar, 0, 100);
    lv_bar_set_value(s_wiz.job_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_wiz.job_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_wiz.job_progress_bar, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_bg_color(s_wiz.job_progress_bar, WIZARD_COLOR_BLUE_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_wiz.job_progress_bar, 4, 0);
    lv_obj_set_style_radius(s_wiz.job_progress_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_wiz.job_progress_bar, 0, 0);

    s_wiz.job_progress_label = lv_label_create(right_panel);
    lv_label_set_text(s_wiz.job_progress_label, "Select a file to preview");
    lv_obj_set_style_text_color(s_wiz.job_progress_label, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_style_text_font(s_wiz.job_progress_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wiz.job_progress_label, LV_ALIGN_BOTTOM_MID, 0, -34);

    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    s_wiz.s_file_count = 0;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && s_wiz.s_file_count < MAX_GCODE_FILES) {
            if (entry->d_name[0] == '.' || !has_supported_extension(entry->d_name)) continue;
            snprintf(s_wiz.s_file_paths[s_wiz.s_file_count],
                     sizeof(s_wiz.s_file_paths[s_wiz.s_file_count]),
                     "%s/%s", BSP_SD_MOUNT_POINT, entry->d_name);
            struct stat st;
            char size_str[32] = "";
            if (stat(s_wiz.s_file_paths[s_wiz.s_file_count], &st) == 0) {
                uint64_t sz = st.st_size;
                if (sz >= 1048576)
                    snprintf(size_str, sizeof(size_str), " (%.1f MB)", sz / 1048576.0);
                else if (sz >= 1024)
                    snprintf(size_str, sizeof(size_str), " (%.0f KB)", sz / 1024.0);
                else
                    snprintf(size_str, sizeof(size_str), " (%u B)", (unsigned)sz);
            }
            char label_buf[256];
            snprintf(label_buf, sizeof(label_buf), "%s%s", entry->d_name, size_str);
            lv_obj_t *btn = lv_list_add_button(s_wiz.file_list, LV_SYMBOL_FILE, label_buf);
            lv_obj_set_style_bg_color(btn, WIZARD_COLOR_INPUT_BG, 0);
            lv_obj_set_style_bg_color(btn, lv_color_darken(WIZARD_COLOR_INPUT_BG, 8), LV_STATE_PRESSED);
            lv_obj_set_style_text_color(btn, WIZARD_COLOR_TEXT, 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, WIZARD_COLOR_LIGHT_GRAY, 0);
            lv_obj_set_style_pad_top(btn, 8, 0);
            lv_obj_set_style_pad_bottom(btn, 8, 0);
            lv_obj_set_style_pad_left(btn, 10, 0);
            lv_obj_set_style_pad_right(btn, 10, 0);
            lv_obj_add_event_cb(btn, event_file_selected, LV_EVENT_CLICKED, (void *)(intptr_t)s_wiz.s_file_count);
            s_wiz.s_file_count++;
        }
        closedir(dir);
    }

    if (s_wiz.s_file_count == 0) {
        lv_list_add_text(s_wiz.file_list, "No G-code files found");
        lv_obj_set_style_text_color(lv_obj_get_child(s_wiz.file_list, -1), WIZARD_COLOR_GRAY, 0);
    }

    build_bottom_bar("Close", event_back_to_splash, "Next", event_file_next, false);
}

/* ─── ORIGIN CHOICE ─── */
static void event_origin_jog(lv_event_t *e)
{
    LV_UNUSED(e);
    s_wiz.wizard_flow = true;
    navigate_to(WIZARD_MANUAL);
}

static void event_origin_probe(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_PROBE);
}

static void event_back_to_file(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_FILE_SELECT);
}

static void event_back_to_splash(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_HOME);
}

static void event_back_to_tool(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_TOOL_SELECT);
}

static void build_origin_choice(void)
{
    clear_content();
    s_wiz.state = WIZARD_ORIGIN_CHOICE;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Set Work Origin");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *desc = wizard_create_label(s_wiz.content, "How would you like to set the origin?", 16);
    lv_obj_set_style_text_color(desc, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *card_row = lv_obj_create(s_wiz.content);
    lv_obj_set_size(card_row, 640, 300);
    lv_obj_set_style_bg_opa(card_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card_row, 0, 0);
    lv_obj_clear_flag(card_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(card_row, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *jog_card = lv_obj_create(card_row);
    lv_obj_set_size(jog_card, 280, 260);
    lv_obj_add_style(jog_card, wizard_get_card_style(), 0);
    lv_obj_align(jog_card, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(jog_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(jog_card, event_origin_jog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *jog_icon = lv_label_create(jog_card);
    lv_label_set_text(jog_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(jog_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(jog_icon, WIZARD_COLOR_BLUE_ACCENT, 0);
    lv_obj_align(jog_icon, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *jog_title = lv_label_create(jog_card);
    lv_label_set_text(jog_title, "Jog to Position");
    lv_obj_set_style_text_font(jog_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(jog_title, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(jog_title, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *jog_desc = lv_label_create(jog_card);
    lv_label_set_text(jog_desc, "Manually move the machine\nto the desired origin point\nand set zero.");
    lv_obj_set_style_text_font(jog_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(jog_desc, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(jog_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(jog_desc, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *probe_card = lv_obj_create(card_row);
    lv_obj_set_size(probe_card, 280, 260);
    lv_obj_add_style(probe_card, wizard_get_card_style(), 0);
    lv_obj_align(probe_card, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(probe_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(probe_card, event_origin_probe, LV_EVENT_CLICKED, NULL);

    lv_obj_t *probe_icon = lv_label_create(probe_card);
    lv_label_set_text(probe_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(probe_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(probe_icon, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_align(probe_icon, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *probe_title = lv_label_create(probe_card);
    lv_label_set_text(probe_title, "Probe");
    lv_obj_set_style_text_font(probe_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(probe_title, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(probe_title, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *probe_desc = lv_label_create(probe_card);
    lv_label_set_text(probe_desc, "Jog above the probe plate,\nthen automatically probe\nX, Y and Z zero.");
    lv_obj_set_style_text_font(probe_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(probe_desc, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_style_text_align(probe_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(probe_desc, LV_ALIGN_TOP_MID, 0, 130);

    build_bottom_bar("Back", event_back_to_file, NULL, NULL, false);
}

/* ─── JOG (reused drawing code) ─── */
static void draw_scaled_polygon(lv_layer_t *layer, lv_obj_t *obj, const float *points, uint32_t point_count, lv_color_t fill)
{
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    float size = (float)LV_MIN(lv_area_get_width(&coords), lv_area_get_height(&coords));
    float scale = size / 200.0f;
    float ox = coords.x1 + ((lv_area_get_width(&coords) - size) * 0.5f);
    float oy = coords.y1 + ((lv_area_get_height(&coords) - size) * 0.5f);
    lv_point_precise_t p[8];
    for (uint32_t i = 0; i < point_count && i < 8; i++) {
        p[i].x = ox + (points[i * 2] * scale);
        p[i].y = oy + (points[i * 2 + 1] * scale);
    }
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    td.bg_opa = LV_OPA_COVER;
    td.bg_color = fill;
    for (uint32_t i = 1; i + 1 < point_count; i++) {
        td.p[0] = p[0]; td.p[1] = p[i]; td.p[2] = p[i + 1];
        lv_draw_triangle(layer, &td);
    }
}

static void draw_scaled_polyline(lv_layer_t *layer, lv_obj_t *obj, const float *points, uint32_t point_count,
                                 lv_color_t color, int32_t width, float vw, float vh)
{
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    float sx = (float)lv_area_get_width(&coords) / vw;
    float sy = (float)lv_area_get_height(&coords) / vh;
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = color;
    ld.width = width;
    ld.round_start = 1;
    ld.round_end = 1;
    for (uint32_t i = 0; i + 1 < point_count; i++) {
        ld.p1.x = coords.x1 + (int32_t)lroundf(points[i * 2] * sx);
        ld.p1.y = coords.y1 + (int32_t)lroundf(points[i * 2 + 1] * sy);
        ld.p2.x = coords.x1 + (int32_t)lroundf(points[(i + 1) * 2] * sx);
        ld.p2.y = coords.y1 + (int32_t)lroundf(points[(i + 1) * 2 + 1] * sy);
        lv_draw_line(layer, &ld);
    }
}

static const float s_jog_xy_gray[] = {180.191f, 140.859f, 140.859f, 180.191f, 100.0f, 100.0f,
                                       59.1408f, 180.191f, 19.8094f, 140.859f, 100.0f, 100.0f,
                                       19.8094f, 59.1409f, 59.1408f, 19.8094f, 100.0f, 100.0f,
                                       140.859f, 19.8094f, 180.191f, 59.1409f, 100.0f, 100.0f};
static const float s_jog_arrow_y_minus[] = {100.0f, 100.0f, 138.268f, 192.388f, 126.136f, 197.413f, 113.132f, 200.0f, 100.0f, 200.0f, 86.8678f, 200.0f, 73.8642f, 197.413f, 61.7316f, 192.388f};
static const float s_jog_arrow_x_minus[] = {100.0f, 100.0f, 7.61205f, 138.268f, 2.58658f, 126.136f, 0.0f, 113.132f, 0.0f, 100.0f, 0.0f, 86.8678f, 2.58658f, 73.8642f, 7.61206f, 61.7316f};
static const float s_jog_arrow_y_plus[]  = {100.0f, 100.0f, 61.7316f, 7.61205f, 73.8642f, 2.58658f, 86.8678f, 0.0f, 100.0f, 0.0f, 113.132f, 0.0f, 126.136f, 2.58658f, 138.268f, 7.61205f};
static const float s_jog_arrow_x_plus[]  = {100.0f, 100.0f, 192.388f, 61.7317f, 197.413f, 73.8642f, 200.0f, 86.8678f, 200.0f, 100.0f, 200.0f, 113.132f, 197.413f, 126.136f, 192.388f, 138.268f};
static const float s_diag_tl[] = {60.0f, 42.0f, 47.0f, 47.0f, 52.0f, 60.0f};
static const float s_diag_tr[] = {140.0f, 42.0f, 153.0f, 47.0f, 148.0f, 60.0f};
static const float s_diag_bl[] = {42.0f, 140.0f, 47.0f, 153.0f, 60.0f, 148.0f};
static const float s_diag_br[] = {148.0f, 140.0f, 153.0f, 153.0f, 140.0f, 148.0f};
static const float s_arrow_up[] = {100.0f, 10.0f, 87.0f, 30.0f, 113.0f, 30.0f};
static const float s_arrow_down[] = {87.0f, 170.0f, 113.0f, 170.0f, 100.0f, 190.0f};
static const float s_arrow_left[] = {10.0f, 100.0f, 30.0f, 87.0f, 30.0f, 113.0f};
static const float s_arrow_right[] = {170.0f, 87.0f, 190.0f, 100.0f, 170.0f, 113.0f};

static void jog_xy_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    float size = (float)LV_MIN(lv_area_get_width(&coords), lv_area_get_height(&coords));
    float scale = size / 200.0f;
    float cx = coords.x1 + (lv_area_get_width(&coords) * 0.5f);
    float cy = coords.y1 + (lv_area_get_height(&coords) * 0.5f);

    draw_scaled_polygon(layer, obj, s_jog_xy_gray, 3, lv_color_hex(0x9CA3AF));
    draw_scaled_polygon(layer, obj, s_jog_xy_gray + 6, 3, lv_color_hex(0x9CA3AF));
    draw_scaled_polygon(layer, obj, s_jog_xy_gray + 12, 3, lv_color_hex(0x9CA3AF));
    draw_scaled_polygon(layer, obj, s_jog_xy_gray + 18, 3, lv_color_hex(0x9CA3AF));
    draw_scaled_polygon(layer, obj, s_jog_arrow_y_minus, 8, WIZARD_COLOR_BLUE_ACCENT);
    draw_scaled_polygon(layer, obj, s_jog_arrow_x_minus, 8, WIZARD_COLOR_BLUE_ACCENT);
    draw_scaled_polygon(layer, obj, s_jog_arrow_y_plus, 8, WIZARD_COLOR_BLUE_ACCENT);
    draw_scaled_polygon(layer, obj, s_jog_arrow_x_plus, 8, WIZARD_COLOR_BLUE_ACCENT);
    draw_scaled_polygon(layer, obj, s_arrow_up, 3, lv_color_hex(0x2d3436));
    draw_scaled_polygon(layer, obj, s_arrow_down, 3, lv_color_hex(0x2d3436));
    draw_scaled_polygon(layer, obj, s_arrow_left, 3, lv_color_hex(0x2d3436));
    draw_scaled_polygon(layer, obj, s_arrow_right, 3, lv_color_hex(0x2d3436));
    draw_scaled_polyline(layer, obj, s_diag_tl, 3, lv_color_hex(0x6B7280), 3, 200.0f, 200.0f);
    draw_scaled_polyline(layer, obj, s_diag_tr, 3, lv_color_hex(0x6B7280), 3, 200.0f, 200.0f);
    draw_scaled_polyline(layer, obj, s_diag_bl, 3, lv_color_hex(0x6B7280), 3, 200.0f, 200.0f);
    draw_scaled_polyline(layer, obj, s_diag_br, 3, lv_color_hex(0x6B7280), 3, 200.0f, 200.0f);

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_COVER;
    rd.bg_color = lv_color_hex(0xF4F4F4);
    rd.radius = LV_RADIUS_CIRCLE;
    rd.border_width = 0;
    lv_area_t ca;
    ca.x1 = (int32_t)(cx - (38.5f * scale));
    ca.y1 = (int32_t)(cy - (38.5f * scale));
    ca.x2 = (int32_t)(cx + (38.5f * scale));
    ca.y2 = (int32_t)(cy + (38.5f * scale));
    lv_draw_rect(layer, &rd, &ca);
}

static void jog_z_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_COVER;
    rd.bg_color = WIZARD_COLOR_BLUE_ACCENT;
    rd.radius = 9;
    rd.border_width = 0;
    lv_area_t ta;
    ta.x1 = coords.x1; ta.y1 = coords.y1;
    ta.x2 = coords.x2; ta.y2 = coords.y1 + (int32_t)lroundf((88.5f / 187.0f) * lv_area_get_height(&coords));
    lv_draw_rect(layer, &rd, &ta);
    lv_area_t ba;
    ba.x1 = coords.x1; ba.y1 = coords.y1 + (int32_t)lroundf((98.5f / 187.0f) * lv_area_get_height(&coords));
    ba.x2 = coords.x2; ba.y2 = coords.y2;
    lv_draw_rect(layer, &rd, &ba);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(0xCECECE);
    ld.width = 1;
    ld.p1.x = coords.x1; ld.p2.x = coords.x2;
    ld.p1.y = coords.y1 + (int32_t)lroundf((93.5f / 187.0f) * lv_area_get_height(&coords));
    ld.p2.y = ld.p1.y;
    lv_draw_line(layer, &ld);
}

static void perform_jog(int action)
{
    if (cnc_link_is_connected()) {
        float dist = 1.0f;
        float feed = 1200.0f;
        switch (action) {
            case JOG_ACTION_X_MINUS: cnc_link_jog('X', -dist, feed); break;
            case JOG_ACTION_X_PLUS:  cnc_link_jog('X', dist, feed); break;
            case JOG_ACTION_Y_PLUS:  cnc_link_jog('Y', dist, feed); break;
            case JOG_ACTION_Y_MINUS: cnc_link_jog('Y', -dist, feed); break;
            case JOG_ACTION_Z_PLUS:  cnc_link_jog('Z', dist, feed); break;
            case JOG_ACTION_Z_MINUS: cnc_link_jog('Z', -dist, feed); break;
            case JOG_ACTION_A_PLUS:  cnc_link_jog('A', dist, feed); break;
            case JOG_ACTION_A_MINUS: cnc_link_jog('A', -dist, feed); break;
        }
    }
}

static void jog_repeat_cb(lv_timer_t *t)
{
    perform_jog((int)(intptr_t)lv_timer_get_user_data(t));
}

static void jog_hold_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        perform_jog(action);
        if (s_wiz.jog_repeat_timer) lv_timer_delete(s_wiz.jog_repeat_timer);
        s_wiz.jog_repeat_action = action;
        s_wiz.jog_repeat_timer = lv_timer_create(jog_repeat_cb, 160, (void *)(intptr_t)action);
        lv_timer_set_repeat_count(s_wiz.jog_repeat_timer, -1);
        lv_timer_set_period(s_wiz.jog_repeat_timer, 160);
        lv_timer_pause(s_wiz.jog_repeat_timer);
        lv_timer_set_period(s_wiz.jog_repeat_timer, 220);
        lv_timer_resume(s_wiz.jog_repeat_timer);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_wiz.jog_repeat_timer) {
            lv_timer_delete(s_wiz.jog_repeat_timer);
            s_wiz.jog_repeat_timer = NULL;
            s_wiz.jog_repeat_action = JOG_ACTION_NONE;
        }
    }
}

static lv_obj_t *jog_overlay_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_coord_t r, int action)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, r, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, jog_hold_cb, LV_EVENT_PRESSED, (void *)(intptr_t)action);
    lv_obj_add_event_cb(btn, jog_hold_cb, LV_EVENT_RELEASED, (void *)(intptr_t)action);
    lv_obj_add_event_cb(btn, jog_hold_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)action);
    return btn;
}

static void event_set_zero(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_line("G10 L20 P1 X0 Y0 Z0");
    s_wiz.origin_x = 0;
    s_wiz.origin_y = 0;
    s_wiz.origin_z = 0;
    s_wiz.origin_set = true;
    if (s_wiz.bottom_next_btn) {
        lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_BLUE_ACCENT, 0);
        lv_obj_clear_state(s_wiz.bottom_next_btn, LV_STATE_DISABLED);
    }
}

static void event_jog_next(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_TOOL_SELECT);
}

static void event_back_to_origin(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_ORIGIN_CHOICE);
}

static void build_jog(void)
{
    clear_content();
    s_wiz.state = WIZARD_JOG;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Jog to Origin");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = wizard_create_label(s_wiz.content, "Use the jog controls to position the tool at the desired origin, then tap Set Zero.", 14);
    lv_obj_set_style_text_color(hint, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_width(hint, 600);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 58);

    lv_obj_t *jog_area = lv_obj_create(s_wiz.content);
    lv_obj_set_size(jog_area, 480, 340);
    lv_obj_set_style_bg_opa(jog_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(jog_area, 0, 0);
    lv_obj_clear_flag(jog_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(jog_area, LV_ALIGN_CENTER, -80, 20);

    lv_obj_t *xy = lv_obj_create(jog_area);
    lv_obj_set_size(xy, 260, 260);
    lv_obj_set_pos(xy, 20, 40);
    lv_obj_set_style_bg_opa(xy, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(xy, 0, 0);
    lv_obj_clear_flag(xy, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(xy, jog_xy_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    jog_overlay_btn(xy, 50, 0, 76, 40, 20, JOG_ACTION_Y_PLUS);
    jog_overlay_btn(xy, 50, 136, 76, 40, 20, JOG_ACTION_Y_MINUS);
    jog_overlay_btn(xy, 0, 50, 40, 76, 20, JOG_ACTION_X_MINUS);
    jog_overlay_btn(xy, 136, 50, 40, 76, 20, JOG_ACTION_X_PLUS);

    lv_obj_t *yl = lv_label_create(xy);
    lv_label_set_text(yl, "Y+"); lv_obj_set_pos(yl, 79, 30);
    lv_obj_set_style_text_color(yl, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(yl, &lv_font_montserrat_18, 0);
    lv_obj_t *ym = lv_label_create(xy);
    lv_label_set_text(ym, "Y-"); lv_obj_set_pos(ym, 79, 130);
    lv_obj_set_style_text_color(ym, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ym, &lv_font_montserrat_18, 0);
    lv_obj_t *xl = lv_label_create(xy);
    lv_label_set_text(xl, "X-"); lv_obj_set_pos(xl, 24, 77);
    lv_obj_set_style_text_color(xl, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(xl, &lv_font_montserrat_18, 0);
    lv_obj_t *xr = lv_label_create(xy);
    lv_label_set_text(xr, "X+"); lv_obj_set_pos(xr, 132, 77);
    lv_obj_set_style_text_color(xr, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(xr, &lv_font_montserrat_18, 0);

    lv_obj_t *zw = lv_obj_create(jog_area);
    lv_obj_set_size(zw, 40, 200);
    lv_obj_set_pos(zw, 320, 70);
    lv_obj_set_style_bg_opa(zw, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zw, 0, 0);
    lv_obj_clear_flag(zw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(zw, jog_z_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    jog_overlay_btn(zw, 0, 0, 40, 75, 10, JOG_ACTION_Z_PLUS);
    jog_overlay_btn(zw, 0, 85, 40, 75, 10, JOG_ACTION_Z_MINUS);

    lv_obj_t *zp = lv_label_create(zw);
    lv_label_set_text(zp, "Z+"); lv_obj_align(zp, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_color(zp, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(zp, &lv_font_montserrat_16, 0);
    lv_obj_t *zm = lv_label_create(zw);
    lv_label_set_text(zm, "Z-"); lv_obj_align(zm, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_text_color(zm, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(zm, &lv_font_montserrat_16, 0);

    lv_obj_t *right_panel = lv_obj_create(s_wiz.content);
    lv_obj_set_size(right_panel, 280, 340);
    lv_obj_add_style(right_panel, wizard_get_card_style(), 0);
    lv_obj_align(right_panel, LV_ALIGN_CENTER, 210, 20);

    lv_obj_t *dro_title = wizard_create_label(right_panel, "Machine Position", 16);
    lv_obj_set_style_text_color(dro_title, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(dro_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *dro_labels[3];
    const char *axis_names[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; i++) {
        dro_labels[i] = lv_label_create(right_panel);
        set_label_textf(dro_labels[i], "%s: 0.00", axis_names[i]);
        lv_obj_set_style_text_font(dro_labels[i], &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(dro_labels[i], WIZARD_COLOR_TEXT, 0);
        lv_obj_align(dro_labels[i], LV_ALIGN_TOP_MID, 0, 40 + i * 40);
    }

    lv_obj_t *zero_btn = lv_button_create(right_panel);
    lv_obj_set_size(zero_btn, 220, 50);
    lv_obj_add_style(zero_btn, wizard_get_accent_btn_style(), 0);
    lv_obj_align(zero_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(zero_btn, event_set_zero, LV_EVENT_CLICKED, NULL);
    lv_obj_t *zbl = lv_label_create(zero_btn);
    lv_label_set_text(zbl, LV_SYMBOL_GPS "  Set Zero");
    lv_obj_set_style_text_font(zbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(zbl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(zbl);

    bool zeroed = s_wiz.origin_set;
    build_bottom_bar("Back", event_back_to_origin, "Next", event_jog_next, zeroed);
    if (!zeroed) {
        lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_DIM_GRAY, 0);
        lv_obj_add_state(s_wiz.bottom_next_btn, LV_STATE_DISABLED);
    }
}

/* ─── PROBE ─── */
static void event_probe_start(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_line("G38.2 X-50 F50");
    cnc_link_send_line("G38.2 Y-50 F50");
    cnc_link_send_line("G38.2 Z-50 F50");
}

static void event_probe_next(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_TOOL_SELECT);
}

static void build_probe(void)
{
    clear_content();
    s_wiz.state = WIZARD_PROBE;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Auto Probe X Y Z");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = wizard_create_label(s_wiz.content, "Jog above the probe plate, then tap Probe. The machine will automatically probe X, Y and Z.", 14);
    lv_obj_set_style_text_color(hint, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_width(hint, 600);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 58);

    lv_obj_t *jog_area = lv_obj_create(s_wiz.content);
    lv_obj_set_size(jog_area, 440, 340);
    lv_obj_set_style_bg_opa(jog_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(jog_area, 0, 0);
    lv_obj_clear_flag(jog_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(jog_area, LV_ALIGN_CENTER, -80, 20);

    lv_coord_t bs = 56;
    lv_coord_t gap = 8;
    lv_coord_t x_off = 10;
    lv_coord_t y_off = 12;
    lv_color_t jog_gray = WIZARD_COLOR_DIM_GRAY;
    lv_color_t jog_blue = WIZARD_COLOR_BLUE_ACCENT;

    create_jog_btn(jog_area, x_off + bs + gap, y_off, bs, "Y+", jog_hold_cb, (void *)JOG_ACTION_Y_PLUS, jog_blue);
    create_jog_btn(jog_area, x_off + bs + gap, y_off + 2*(bs+gap), bs, "Y-", jog_hold_cb, (void *)JOG_ACTION_Y_MINUS, jog_blue);
    create_jog_btn(jog_area, x_off, y_off + bs + gap, bs, "X-", jog_hold_cb, (void *)JOG_ACTION_X_MINUS, jog_blue);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off + bs + gap, bs, "X+", jog_hold_cb, (void *)JOG_ACTION_X_PLUS, jog_blue);

    create_jog_btn(jog_area, x_off, y_off, bs, "NW", event_jog_diag, (void *)"X- Y-", jog_gray);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off, bs, "NE", event_jog_diag, (void *)"X+ Y-", jog_gray);
    create_jog_btn(jog_area, x_off, y_off + 2*(bs+gap), bs, "SW", event_jog_diag, (void *)"X- Y+", jog_gray);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off + 2*(bs+gap), bs, "SE", event_jog_diag, (void *)"X+ Y+", jog_gray);

    lv_obj_t *stop_btn = lv_button_create(jog_area);
    lv_obj_set_size(stop_btn, bs, bs);
    lv_obj_set_pos(stop_btn, x_off + bs + gap, y_off + bs + gap);
    lv_obj_set_style_bg_color(stop_btn, WIZARD_COLOR_RED_DANGER, 0);
    lv_obj_set_style_radius(stop_btn, bs / 4, 0);
    lv_obj_set_style_border_width(stop_btn, 0, 0);
    lv_obj_add_event_cb(stop_btn, event_jog_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(stop_btn);
    lv_label_set_text(sl, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(sl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(sl);

    lv_coord_t za_x = x_off + 3*bs + 2*gap + 12;
    lv_coord_t za_y = y_off;
    create_jog_btn(jog_area, za_x, za_y, bs, "Z+", jog_hold_cb, (void *)JOG_ACTION_Z_PLUS, jog_blue);
    create_jog_btn(jog_area, za_x + bs + gap, za_y, bs, "A+", jog_hold_cb, (void *)JOG_ACTION_A_PLUS, jog_blue);
    create_jog_btn(jog_area, za_x, za_y + bs + gap, bs, "Z-", jog_hold_cb, (void *)JOG_ACTION_Z_MINUS, jog_blue);
    create_jog_btn(jog_area, za_x + bs + gap, za_y + bs + gap, bs, "A-", jog_hold_cb, (void *)JOG_ACTION_A_MINUS, jog_blue);

    lv_obj_t *right_panel = lv_obj_create(s_wiz.content);
    lv_obj_set_size(right_panel, 280, 340);
    lv_obj_add_style(right_panel, wizard_get_card_style(), 0);
    lv_obj_align(right_panel, LV_ALIGN_CENTER, 210, 20);

    lv_obj_t *probe_title = wizard_create_label(right_panel, "Probe Setup", 18);
    lv_obj_align(probe_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *probe_info = wizard_create_label(right_panel,
        "1. Place probe plate on\n"
        "   the spoilboard\n"
        "2. Jog above the plate\n"
        "3. Tap Probe to auto probe\n"
        "   X, Y and Z", 14);
    lv_obj_set_style_text_color(probe_info, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(probe_info, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *probe_btn = lv_button_create(right_panel);
    lv_obj_set_size(probe_btn, 220, 50);
    lv_obj_add_style(probe_btn, wizard_get_accent_btn_style(), 0);
    lv_obj_set_style_bg_color(probe_btn, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_align(probe_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(probe_btn, event_probe_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pbl = lv_label_create(probe_btn);
    lv_label_set_text(pbl, LV_SYMBOL_REFRESH "  Probe XYZ");
    lv_obj_set_style_text_color(pbl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(pbl);

    build_bottom_bar("Back", event_back_to_origin, "Done", event_probe_next, true);
}

/* ─── TOOL SELECT ─── */
static const char *s_default_tools[NUM_TOOLS] = {
    "1/4\" 4-flute Upcut",
    "1/8\" 2-flute Downcut",
    "60deg 16mm V-Bit",
    "90deg 12mm V-Bit",
    "3.175mm Ball End",
    "6mm Ball End",
    "Tapered 1/4\" 30deg",
    "Surfacing 1\" Bit",
    "1/4\" Compression",
    "3mm Single Flute",
    "1/16\" Engraving",
    "Custom Tool",
};

static void event_tool_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    s_wiz.selected_tool = (int)lv_dropdown_get_selected(s_wiz.tool_dropdown);
    persist_save_int("tool", s_wiz.selected_tool);
}

static void event_tool_next(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_READY);
}

static void build_tool_select(void)
{
    clear_content();
    s_wiz.state = WIZARD_TOOL_SELECT;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Select Tool");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *desc = wizard_create_label(s_wiz.content, "Choose the tool you are using for this job.", 14);
    lv_obj_set_style_text_color(desc, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *card = wizard_create_card(s_wiz.content, 500, 320);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *dd_label = wizard_create_label(card, "Tool", 16);
    lv_obj_set_style_text_color(dd_label, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(dd_label, LV_ALIGN_TOP_LEFT, 0, 0);

    char options[2048] = "";
    for (int i = 0; i < NUM_TOOLS; i++) {
        if (i > 0) strlcat(options, "\n", sizeof(options));
        char slot_opt[64];
        snprintf(slot_opt, sizeof(slot_opt), "[Slot %d] %s", i + 1, s_default_tools[i]);
        strlcat(options, slot_opt, sizeof(options));
    }

    s_wiz.tool_dropdown = lv_dropdown_create(card);
    lv_dropdown_set_options(s_wiz.tool_dropdown, options);
    lv_dropdown_set_selected(s_wiz.tool_dropdown, (uint16_t)s_wiz.selected_tool);
    lv_obj_set_size(s_wiz.tool_dropdown, lv_pct(100), 48);
    lv_obj_align(s_wiz.tool_dropdown, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(s_wiz.tool_dropdown, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_color(s_wiz.tool_dropdown, WIZARD_COLOR_INPUT_BG, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_wiz.tool_dropdown, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(s_wiz.tool_dropdown, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_border_width(s_wiz.tool_dropdown, 1, 0);
    lv_obj_set_style_radius(s_wiz.tool_dropdown, 8, 0);
    lv_obj_set_style_pad_left(s_wiz.tool_dropdown, 12, 0);
    lv_obj_set_style_pad_right(s_wiz.tool_dropdown, 12, 0);
    lv_obj_add_event_cb(s_wiz.tool_dropdown, event_tool_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *info_label = wizard_create_label(card,
        "Set up your tool library and default feeds/speeds\nin the Settings menu.", 14);
    lv_obj_set_style_text_color(info_label, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    build_bottom_bar("Back", event_back_to_origin, "Next", event_tool_next, true);
}

/* ─── READY ─── */
static void event_start_job(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_RUNNING);
}

static void build_ready(void)
{
    clear_content();
    s_wiz.state = WIZARD_READY;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Ready to Run?");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *card = wizard_create_card(s_wiz.content, 520, 200);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -30);

    const char *labels[2] = {"File", "Tool"};
    char values[2][64];
    snprintf(values[0], sizeof(values[0]), "%s", path_basename(s_wiz.selected_file));
    snprintf(values[1], sizeof(values[1]), "%s", s_default_tools[s_wiz.selected_tool]);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, lv_pct(100), 36);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 16 + i * 52);

        lv_obj_t *lbl = wizard_create_label(row, labels[i], 16);
        lv_obj_set_style_text_color(lbl, WIZARD_COLOR_GRAY, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);

        lv_obj_t *val = wizard_create_label(row, values[i], 16);
        lv_obj_set_style_text_color(val, WIZARD_COLOR_TEXT, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -16, 0);
    }

    lv_obj_t *start_btn = wizard_create_green_btn(s_wiz.content, LV_SYMBOL_PLAY "  Start Job", event_start_job);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 160);

    build_bottom_bar("Back", event_back_to_tool, NULL, NULL, false);
}

/* ─── RUNNING ─── */
static void event_pause(lv_event_t *e)
{
    LV_UNUSED(e);
    s_wiz.paused = !s_wiz.paused;
    if (s_wiz.paused) {
        cnc_link_send_realtime(0x21);
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_label_set_text(lbl, LV_SYMBOL_PLAY " Resume");
        }
    } else {
        cnc_link_send_realtime(0x7E);
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_label_set_text(lbl, LV_SYMBOL_PAUSE " Pause");
        }
    }
}

static void event_abort(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_realtime(0x21);
    vTaskDelay(pdMS_TO_TICKS(100));
    cnc_link_soft_reset();
    navigate_to(WIZARD_FILE_SELECT);
}

static void event_feed_adj(lv_event_t *e)
{
    intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    s_wiz.feed_override_pct += (int)delta;
    if (s_wiz.feed_override_pct < 10) s_wiz.feed_override_pct = 10;
    if (s_wiz.feed_override_pct > 200) s_wiz.feed_override_pct = 200;
    set_label_textf(s_wiz.feed_label, "%d%%", s_wiz.feed_override_pct);
    persist_save_int("feed", s_wiz.feed_override_pct);
    if (delta >= 10) cnc_link_send_realtime(0x91);
    else if (delta <= -10) cnc_link_send_realtime(0x92);
    else if (delta > 0) cnc_link_send_realtime(0x93);
    else cnc_link_send_realtime(0x94);
}

static void event_speed_adj(lv_event_t *e)
{
    intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    s_wiz.speed_override_pct += (int)delta;
    if (s_wiz.speed_override_pct < 10) s_wiz.speed_override_pct = 10;
    if (s_wiz.speed_override_pct > 200) s_wiz.speed_override_pct = 200;
    set_label_textf(s_wiz.speed_label, "%d%%", s_wiz.speed_override_pct);
    persist_save_int("speed", s_wiz.speed_override_pct);
    if (delta >= 10) cnc_link_send_realtime(0x96);
    else if (delta <= -10) cnc_link_send_realtime(0x97);
    else if (delta > 0) cnc_link_send_realtime(0x98);
    else cnc_link_send_realtime(0x99);
}

static void sim_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_wiz.sim_step += 60;
    int count = gcode_viewer_get_vertex_count();
    if (s_wiz.sim_step >= count - 1) {
        s_wiz.sim_step = 1;
    }
    gcode_viewer_set_render_count(s_wiz.sim_step);
}

static void stream_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    char line[256];
    int max_per_tick = 10;

    if (!s_wiz.stream_file && cnc_link_get_pending_bytes() == 0) {
        if (s_wiz.stream_timer) {
            lv_timer_t *st = s_wiz.stream_timer;
            s_wiz.stream_timer = NULL;
            lv_timer_delete(st);
        }
        navigate_to(WIZARD_COMPLETE);
        return;
    }

    if (!s_wiz.stream_file) {
        return;
    }

    while (cnc_link_can_send() && max_per_tick-- > 0) {
        if (!fgets(line, sizeof(line), s_wiz.stream_file)) {
            fclose(s_wiz.stream_file);
            s_wiz.stream_file = NULL;
            break;
        }
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == ';' || line[0] == '(') {
            continue;
        }
        if (cnc_link_send_line_counted(line) != ESP_OK) {
            break;
        }
        s_wiz.stream_lines_sent++;
    }

    if (s_wiz.stream_file) {
        long pos = ftell(s_wiz.stream_file);
        struct stat st;
        if (stat(s_wiz.selected_file, &st) == 0 && st.st_size > 0) {
            lv_bar_set_value(s_wiz.job_progress_bar, (int)(pos * 100 / st.st_size), LV_ANIM_OFF);
        }
    }
    set_label_textf(s_wiz.job_progress_label, "Sent: %d  Acked: %lu", s_wiz.stream_lines_sent, (unsigned long)cnc_link_get_acked_count());
}

static void build_running(void)
{
    clear_content();
    s_wiz.state = WIZARD_RUNNING;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_set_style_bg_color(s_wiz.content, WIZARD_COLOR_DARK_BG, 0);
    lv_obj_set_style_border_width(s_wiz.content, 0, 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    bool sim = !cnc_link_is_connected();

    /* ── Job status card ── */
    lv_obj_t *job_card = lv_obj_create(s_wiz.content);
    lv_obj_set_size(job_card, lv_pct(100), 120);
    lv_obj_add_style(job_card, wizard_get_card_style(), 0);
    lv_obj_align(job_card, LV_ALIGN_TOP_MID, 0, 10);

    /* Filename */
    lv_obj_t *file_lbl = lv_label_create(job_card);
    lv_label_set_text(file_lbl, path_basename(s_wiz.selected_file));
    lv_obj_set_style_text_color(file_lbl, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(file_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(file_lbl, LV_ALIGN_TOP_MID, 0, 8);

    /* Status + progress */
    lv_obj_t *status_lbl = lv_label_create(job_card);
    lv_label_set_text(status_lbl, sim ? LV_SYMBOL_PLAY " SIMULATION" : LV_SYMBOL_PLAY " STREAMING");
    lv_obj_set_style_text_color(status_lbl, sim ? WIZARD_COLOR_ORANGE : WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(status_lbl, LV_ALIGN_TOP_LEFT, 16, 44);

    s_wiz.job_progress_bar = lv_bar_create(job_card);
    lv_obj_set_size(s_wiz.job_progress_bar, 400, 12);
    lv_obj_align(s_wiz.job_progress_bar, LV_ALIGN_TOP_MID, 0, 44);
    lv_bar_set_range(s_wiz.job_progress_bar, 0, 100);
    lv_bar_set_value(s_wiz.job_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_wiz.job_progress_bar, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_bg_color(s_wiz.job_progress_bar, WIZARD_COLOR_BLUE_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_wiz.job_progress_bar, 6, 0);
    lv_obj_set_style_radius(s_wiz.job_progress_bar, 6, LV_PART_INDICATOR);

    s_wiz.job_progress_label = lv_label_create(job_card);
    lv_label_set_text(s_wiz.job_progress_label, "Starting...");
    lv_obj_set_style_text_color(s_wiz.job_progress_label, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_style_text_font(s_wiz.job_progress_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wiz.job_progress_label, LV_ALIGN_TOP_RIGHT, -16, 44);

    if (!sim) {
        s_wiz.stream_file = fopen(s_wiz.selected_file, "r");
        s_wiz.stream_lines_sent = 0;
        if (s_wiz.stream_file) {
            s_wiz.stream_timer = lv_timer_create(stream_timer_cb, 50, NULL);
        }
    }

    /* ── DRO card ── */
    lv_obj_t *dro_card = lv_obj_create(s_wiz.content);
    lv_obj_set_size(dro_card, lv_pct(100), 220);
    lv_obj_add_style(dro_card, wizard_get_card_style(), 0);
    lv_obj_align(dro_card, LV_ALIGN_TOP_MID, 0, 145);

    static const lv_font_t *axis_font = &lv_font_montserrat_28;
    static const lv_coord_t axis_w = (1024 / 4) - 20;
    const char *axis_names[4] = {"X", "Y", "Z", "A"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *box = lv_obj_create(dro_card);
        lv_obj_set_size(box, axis_w, 80);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(box, LV_ALIGN_LEFT_MID, 10 + i * (axis_w + 16), 0);

        lv_obj_t *n = lv_label_create(box);
        lv_label_set_text_fmt(n, "%s", axis_names[i]);
        lv_obj_set_style_text_color(n, WIZARD_COLOR_GRAY, 0);
        lv_obj_set_style_text_font(n, &lv_font_montserrat_18, 0);
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t *val = lv_label_create(box);
        lv_label_set_text(val, "0.000");
        lv_obj_set_style_text_color(val, WIZARD_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(val, axis_font, 0);
        lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    /* ── Overlay controls ── */
    lv_obj_t *overlay = lv_obj_create(s_wiz.content);
    lv_obj_set_size(overlay, lv_pct(100), 88);
    lv_obj_set_style_bg_color(overlay, WIZARD_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(overlay, 1, 0);
    lv_obj_set_style_border_color(overlay, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Pause / Abort */
    lv_obj_t *p_btn = lv_button_create(overlay);
    lv_obj_set_size(p_btn, 120, 52);
    lv_obj_set_style_bg_color(p_btn, WIZARD_COLOR_ORANGE, 0);
    lv_obj_set_style_radius(p_btn, 8, 0);
    lv_obj_align(p_btn, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_add_event_cb(p_btn, event_pause, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(p_btn);
    lv_label_set_text(pl, LV_SYMBOL_PAUSE " Pause");
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(pl);

    lv_obj_t *a_btn = lv_button_create(overlay);
    lv_obj_set_size(a_btn, 120, 52);
    lv_obj_set_style_bg_color(a_btn, WIZARD_COLOR_RED_DANGER, 0);
    lv_obj_set_style_radius(a_btn, 8, 0);
    lv_obj_align(a_btn, LV_ALIGN_LEFT_MID, 148, 0);
    lv_obj_add_event_cb(a_btn, event_abort, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(a_btn);
    lv_label_set_text(al, LV_SYMBOL_STOP " Abort");
    lv_obj_set_style_text_font(al, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(al, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(al);

    /* Feed overrides */
    lv_obj_t *feed_box = lv_obj_create(overlay);
    lv_obj_set_size(feed_box, 310, 52);
    lv_obj_set_style_bg_opa(feed_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feed_box, 0, 0);
    lv_obj_clear_flag(feed_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(feed_box, LV_ALIGN_LEFT_MID, 300, 0);

    lv_obj_t *fmin = lv_button_create(feed_box);
    lv_obj_set_size(fmin, 48, 48);
    lv_obj_set_style_bg_color(fmin, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_border_color(fmin, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_border_width(fmin, 1, 0);
    lv_obj_set_style_radius(fmin, 8, 0);
    lv_obj_align(fmin, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(fmin, event_feed_adj, LV_EVENT_CLICKED, (void *)-10);
    lv_obj_t *fml = lv_label_create(fmin);
    lv_label_set_text(fml, "-");
    lv_obj_set_style_text_font(fml, &lv_font_montserrat_22, 0);
    lv_obj_center(fml);
    lv_obj_set_style_text_color(fml, WIZARD_COLOR_TEXT, 0);

    lv_obj_t *f_lbl = lv_label_create(feed_box);
    lv_label_set_text(f_lbl, "Feed");
    lv_obj_set_style_text_color(f_lbl, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_style_text_font(f_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(f_lbl, LV_ALIGN_LEFT_MID, 60, -10);

    s_wiz.feed_label = lv_label_create(feed_box);
    set_label_textf(s_wiz.feed_label, "%d%%", s_wiz.feed_override_pct);
    lv_obj_set_style_text_color(s_wiz.feed_label, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wiz.feed_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_wiz.feed_label, LV_ALIGN_LEFT_MID, 60, 10);

    lv_obj_t *fplus = lv_button_create(feed_box);
    lv_obj_set_size(fplus, 48, 48);
    lv_obj_set_style_bg_color(fplus, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_border_color(fplus, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_border_width(fplus, 1, 0);
    lv_obj_set_style_radius(fplus, 8, 0);
    lv_obj_align(fplus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(fplus, event_feed_adj, LV_EVENT_CLICKED, (void *)10);
    lv_obj_t *fpl = lv_label_create(fplus);
    lv_label_set_text(fpl, "+");
    lv_obj_set_style_text_font(fpl, &lv_font_montserrat_22, 0);
    lv_obj_center(fpl);
    lv_obj_set_style_text_color(fpl, WIZARD_COLOR_TEXT, 0);

    /* Speed overrides */
    lv_obj_t *speed_box = lv_obj_create(overlay);
    lv_obj_set_size(speed_box, 310, 52);
    lv_obj_set_style_bg_opa(speed_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(speed_box, 0, 0);
    lv_obj_clear_flag(speed_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(speed_box, LV_ALIGN_LEFT_MID, 630, 0);

    lv_obj_t *smin = lv_button_create(speed_box);
    lv_obj_set_size(smin, 48, 48);
    lv_obj_set_style_bg_color(smin, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_border_color(smin, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_border_width(smin, 1, 0);
    lv_obj_set_style_radius(smin, 8, 0);
    lv_obj_align(smin, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(smin, event_speed_adj, LV_EVENT_CLICKED, (void *)-10);
    lv_obj_t *sml = lv_label_create(smin);
    lv_label_set_text(sml, "-");
    lv_obj_set_style_text_font(sml, &lv_font_montserrat_22, 0);
    lv_obj_center(sml);
    lv_obj_set_style_text_color(sml, WIZARD_COLOR_TEXT, 0);

    lv_obj_t *s_lbl = lv_label_create(speed_box);
    lv_label_set_text(s_lbl, "Speed");
    lv_obj_set_style_text_color(s_lbl, WIZARD_COLOR_GRAY, 0);
    lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl, LV_ALIGN_LEFT_MID, 60, -10);

    s_wiz.speed_label = lv_label_create(speed_box);
    set_label_textf(s_wiz.speed_label, "%d%%", s_wiz.speed_override_pct);
    lv_obj_set_style_text_color(s_wiz.speed_label, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wiz.speed_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_wiz.speed_label, LV_ALIGN_LEFT_MID, 60, 10);

    lv_obj_t *splus = lv_button_create(speed_box);
    lv_obj_set_size(splus, 48, 48);
    lv_obj_set_style_bg_color(splus, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_border_color(splus, WIZARD_COLOR_LIGHT_GRAY, 0);
    lv_obj_set_style_border_width(splus, 1, 0);
    lv_obj_set_style_radius(splus, 8, 0);
    lv_obj_align(splus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(splus, event_speed_adj, LV_EVENT_CLICKED, (void *)10);
    lv_obj_t *spl = lv_label_create(splus);
    lv_label_set_text(spl, "+");
    lv_obj_set_style_text_font(spl, &lv_font_montserrat_22, 0);
    lv_obj_center(spl);
    lv_obj_set_style_text_color(spl, WIZARD_COLOR_TEXT, 0);
}

/* ─── COMPLETE ─── */
static void event_new_job(lv_event_t *e)
{
    LV_UNUSED(e);
    navigate_to(WIZARD_FILE_SELECT);
}

static void build_complete(void)
{
    clear_content();
    s_wiz.state = WIZARD_COMPLETE;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_set_style_bg_color(s_wiz.content, WIZARD_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_wiz.content, 0, 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *icon = lv_label_create(s_wiz.content);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(icon, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *complete = wizard_create_label(s_wiz.content, "JOB COMPLETE", 32);
    lv_obj_set_style_text_color(complete, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_align(complete, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *msg = wizard_create_label(s_wiz.content, "Congratulations! The job finished successfully.", 16);
    lv_obj_set_style_text_color(msg, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *new_btn = wizard_create_accent_btn(s_wiz.content, "New Job", event_new_job);
    lv_obj_set_size(new_btn, 220, 52);
    lv_obj_align(new_btn, LV_ALIGN_CENTER, 0, 110);
}

/* ─── HOME ─── */
static void event_home_run_job(lv_event_t *e) { LV_UNUSED(e); navigate_to(WIZARD_FILE_SELECT); }
static void event_home_manual(lv_event_t *e) { LV_UNUSED(e); s_wiz.wizard_flow = false; navigate_to(WIZARD_MANUAL); }
static void event_home_tools(lv_event_t *e) { LV_UNUSED(e); navigate_to(WIZARD_TOOLS); }
static void event_home_setup(lv_event_t *e) { LV_UNUSED(e); navigate_to(WIZARD_SETUP); }
static void event_back_to_home(lv_event_t *e) { LV_UNUSED(e); navigate_to(WIZARD_HOME); }

static void build_home(void)
{
    clear_content();
    s_wiz.state = WIZARD_HOME;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "uGsender");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *subtitle = wizard_create_label(s_wiz.content, "What would you like to do?", 16);
    lv_obj_set_style_text_color(subtitle, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *grid = lv_obj_create(s_wiz.content);
    lv_obj_set_size(grid, 720, 420);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 20);

    typedef struct { const char *icon; const char *title; const char *desc; lv_event_cb_t cb; } home_card_t;
    home_card_t cards[4] = {
        {LV_SYMBOL_PLAY, "Run a Job", "Select and run\na G-code file", event_home_run_job},
        {LV_SYMBOL_CHARGE, "Manual Control", "Jog, probe, and\ncontrol the machine", event_home_manual},
        {LV_SYMBOL_COPY, "Tools & Macros", "Surfacing, calibration,\nand utilities", event_home_tools},
        {LV_SYMBOL_SETTINGS, "Machine Setup", "Configure WiFi,\nprofile and hardware", event_home_setup},
    };

    lv_coord_t card_w = 310, card_h = 170, gap = 24;
    for (int i = 0; i < 4; i++) {
        int col = i % 2, row = i / 2;
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_add_style(card, wizard_get_card_style(), 0);
        lv_obj_set_pos(card, col * (card_w + gap), row * (card_h + gap));
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, cards[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, cards[i].icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(icon, WIZARD_COLOR_BLUE_ACCENT, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 14, 14);

        lv_obj_t *tt = lv_label_create(card);
        lv_label_set_text(tt, cards[i].title);
        lv_obj_set_style_text_font(tt, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(tt, WIZARD_COLOR_TEXT, 0);
        lv_obj_align(tt, LV_ALIGN_TOP_LEFT, 54, 16);

        lv_obj_t *dd = lv_label_create(card);
        lv_label_set_text(dd, cards[i].desc);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(dd, WIZARD_COLOR_GRAY, 0);
        lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 14, 52);
    }
}

/* ─── MANUAL CONTROL ─── */
static void event_spindle_on(lv_event_t *e)
{
    LV_UNUSED(e);
    char buf[64];
    snprintf(buf, sizeof(buf), "M3 S%d", s_wiz.spindle_speed);
    cnc_link_send_line(buf);
}
static void event_spindle_off(lv_event_t *e) { LV_UNUSED(e); cnc_link_send_line("M5"); }
static void event_spindle_speed_adj(lv_event_t *e)
{
    intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    s_wiz.spindle_speed += (int)delta * 1000;
    if (s_wiz.spindle_speed < 1000) s_wiz.spindle_speed = 1000;
    if (s_wiz.spindle_speed > 24000) s_wiz.spindle_speed = 24000;
    set_label_textf(s_wiz.spindle_speed_display, "%d", s_wiz.spindle_speed);
}
static void event_spindle_speed_click(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_wiz.spindle_modal_bg) {
        lv_obj_clear_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_wiz.spindle_modal_bg);
        lv_keyboard_set_textarea(s_wiz.spindle_kb, s_wiz.spindle_speed_ta);
        char cur[16];
        snprintf(cur, sizeof(cur), "%d", s_wiz.spindle_speed);
        lv_textarea_set_text(s_wiz.spindle_speed_ta, cur);
        lv_obj_add_state(s_wiz.spindle_speed_ta, LV_STATE_FOCUSED);
    }
}
static void event_spindle_speed_done(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *txt = lv_textarea_get_text(s_wiz.spindle_speed_ta);
    if (txt && strlen(txt) > 0) {
        int val = atoi(txt);
        if (val >= 1000 && val <= 24000) {
            s_wiz.spindle_speed = val;
            set_label_textf(s_wiz.spindle_speed_display, "%d", s_wiz.spindle_speed);
            persist_save_int("spindle", val);
        }
    }
    if (s_wiz.spindle_modal_bg) {
        lv_obj_add_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_HIDDEN);
    }
}
static void event_manual_probe(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_line("G38.2 Z-50 F50");
}
static void event_manual_home_all(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_line("$H");
}
static void event_manual_set_zero(lv_event_t *e)
{
    const char *axis = (const char *)lv_event_get_user_data(e);
    char buf[32];
    snprintf(buf, sizeof(buf), "G10 L20 P1 %s0", axis);
    cnc_link_send_line(buf);
}
static void event_manual_go_zero(lv_event_t *e)
{
    const char *axis = (const char *)lv_event_get_user_data(e);
    float target = 0;
    switch (axis[0]) {
        case 'X': target = s_wiz.target_x; break;
        case 'Y': target = s_wiz.target_y; break;
        case 'Z': target = s_wiz.target_z; break;
        case 'A': target = s_wiz.target_a; break;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "G0 %s%.3f", axis, (double)target);
    cnc_link_send_line(buf);
}

static void event_target_done(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *txt = lv_textarea_get_text(s_wiz.target_ta);
    float val = txt && strlen(txt) > 0 ? atof(txt) : 0;
    switch (s_wiz.target_axis) {
        case 'X': s_wiz.target_x = val; break;
        case 'Y': s_wiz.target_y = val; break;
        case 'Z': s_wiz.target_z = val; break;
        case 'A': s_wiz.target_a = val; break;
    }
    set_label_textf(s_wiz.target_title, "Set %c Target: %.3f", s_wiz.target_axis, (double)val);
    if (s_wiz.target_modal_bg) {
        lv_obj_add_flag(s_wiz.target_modal_bg, LV_OBJ_FLAG_HIDDEN);
    }
}

static void event_dro_value_click(lv_event_t *e)
{
    const char *axis = (const char *)lv_event_get_user_data(e);
    if (!s_wiz.target_modal_bg) return;
    s_wiz.target_axis = axis[0];
    float cur = 0;
    switch (axis[0]) {
        case 'X': cur = s_wiz.target_x; break;
        case 'Y': cur = s_wiz.target_y; break;
        case 'Z': cur = s_wiz.target_z; break;
        case 'A': cur = s_wiz.target_a; break;
    }
    set_label_textf(s_wiz.target_title, "Set %c Target: %.3f", axis[0], (double)cur);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", (double)cur);
    lv_textarea_set_text(s_wiz.target_ta, buf);
    lv_keyboard_set_textarea(s_wiz.target_kb, s_wiz.target_ta);
    lv_obj_clear_flag(s_wiz.target_modal_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wiz.target_modal_bg);
    lv_obj_add_state(s_wiz.target_ta, LV_STATE_FOCUSED);
}

static lv_obj_t *dro_create_row(lv_obj_t *parent, const char *axis, lv_obj_t **val_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *set = lv_button_create(row);
    lv_obj_set_size(set, 60, 34);
    lv_obj_set_style_bg_color(set, WIZARD_COLOR_BG, 0);
    lv_obj_set_style_radius(set, 6, 0);
    lv_obj_set_style_border_width(set, 1, 0);
    lv_obj_set_style_border_color(set, WIZARD_COLOR_DIM_GRAY, 0);
    lv_obj_align(set, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(set, event_manual_set_zero, LV_EVENT_CLICKED, (void *)axis);
    lv_obj_t *sl = lv_label_create(set);
    set_label_textf(sl, "Set %s0", axis);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sl, WIZARD_COLOR_TEXT, 0);
    lv_obj_center(sl);

    *val_label = lv_label_create(row);
    set_label_textf(*val_label, "%s: 0.00", axis);
    lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(*val_label, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(*val_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(*val_label, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_size(*val_label, 140, 34);
    lv_obj_set_style_radius(*val_label, 6, 0);
    lv_obj_set_style_border_width(*val_label, 1, 0);
    lv_obj_set_style_border_color(*val_label, WIZARD_COLOR_DIM_GRAY, 0);
    lv_obj_set_style_text_align(*val_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(*val_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(*val_label, event_dro_value_click, LV_EVENT_CLICKED, (void *)axis);

    lv_obj_t *go = lv_button_create(row);
    lv_obj_set_size(go, 60, 34);
    lv_obj_set_style_bg_color(go, WIZARD_COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_radius(go, 6, 0);
    lv_obj_set_style_border_width(go, 0, 0);
    lv_obj_align(go, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(go, event_manual_go_zero, LV_EVENT_CLICKED, (void *)axis);
    lv_obj_t *gl = lv_label_create(go);
    set_label_textf(gl, "Go %s0", axis);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(gl);

    return row;
}

static void event_jog_diag(lv_event_t *e)
{
    const char *data = (const char *)lv_event_get_user_data(e);
    char buf[64];
    snprintf(buf, sizeof(buf), "$J=G91 %s F3000", data);
    cnc_link_send_line(buf);
}

static void event_jog_stop(lv_event_t *e)
{
    LV_UNUSED(e);
    cnc_link_send_realtime(0x85);
}

static lv_obj_t *create_jog_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t size, const char *label, lv_event_cb_t cb, void *udata, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(color, 40), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, size / 4, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, udata);
    lv_obj_add_event_cb(btn, jog_hold_cb, LV_EVENT_RELEASED, udata);
    lv_obj_add_event_cb(btn, jog_hold_cb, LV_EVENT_PRESS_LOST, udata);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(lbl);
    return btn;
}

static void build_manual(void)
{
    clear_content();
    s_wiz.state = WIZARD_MANUAL;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);



    /* ── LEFT: DRO ── */
    lv_obj_t *dro_card = wizard_create_card(s_wiz.content, 340, 420);
    lv_obj_align(dro_card, LV_ALIGN_TOP_LEFT, 2, 8);
    lv_obj_clear_flag(dro_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dt = wizard_create_label(dro_card, "Machine Position", 16);
    lv_obj_set_style_text_color(dt, WIZARD_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(dt, LV_ALIGN_TOP_MID, 0, 6);

    const char *axes[] = {"X", "Y", "Z", "A", "F", "S"};
    lv_obj_t **dro_labels[] = {&s_wiz.man_dro_x, &s_wiz.man_dro_y, &s_wiz.man_dro_z, &s_wiz.man_dro_a, NULL, NULL};
    int num_axes = 6;
    lv_obj_t *feed_label = NULL, *spindle_label = NULL;
    for (int i = 0; i < num_axes; i++) {
        if (i < 4) {
            lv_obj_t *row = dro_create_row(dro_card, axes[i], dro_labels[i]);
            lv_obj_align(row, LV_ALIGN_TOP_MID, -10, 48 + i * 56);
        } else if (i == 4) {
            lv_obj_t *row = lv_obj_create(dro_card);
            lv_obj_set_size(row, lv_pct(100), 44);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(row, LV_ALIGN_TOP_MID, -10, 48 + i * 56);
            lv_obj_t *fl = lv_label_create(row);
            lv_label_set_text(fl, "F");
            lv_obj_set_style_text_font(fl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(fl, WIZARD_COLOR_TEXT_SECONDARY, 0);
            lv_obj_align(fl, LV_ALIGN_LEFT_MID, 0, 0);
            feed_label = lv_label_create(row);
            set_label_textf(feed_label, "F: 0");
            lv_obj_set_style_text_font(feed_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(feed_label, WIZARD_COLOR_TEXT, 0);
            lv_obj_set_style_bg_color(feed_label, WIZARD_COLOR_INPUT_BG, 0);
            lv_obj_set_size(feed_label, 140, 34);
            lv_obj_set_style_radius(feed_label, 6, 0);
            lv_obj_set_style_border_width(feed_label, 1, 0);
            lv_obj_set_style_border_color(feed_label, WIZARD_COLOR_DIM_GRAY, 0);
            lv_obj_set_style_text_align(feed_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(feed_label, LV_ALIGN_CENTER, 0, 0);
        } else {
            lv_obj_t *row = lv_obj_create(dro_card);
            lv_obj_set_size(row, lv_pct(100), 44);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(row, LV_ALIGN_TOP_MID, -10, 48 + i * 56);
            lv_obj_t *sl = lv_label_create(row);
            lv_label_set_text(sl, "S");
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(sl, WIZARD_COLOR_TEXT_SECONDARY, 0);
            lv_obj_align(sl, LV_ALIGN_LEFT_MID, 0, 0);
            spindle_label = lv_label_create(row);
            set_label_textf(spindle_label, "S: 0");
            lv_obj_set_style_text_font(spindle_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(spindle_label, WIZARD_COLOR_TEXT, 0);
            lv_obj_set_style_bg_color(spindle_label, WIZARD_COLOR_INPUT_BG, 0);
            lv_obj_set_size(spindle_label, 140, 34);
            lv_obj_set_style_radius(spindle_label, 6, 0);
            lv_obj_set_style_border_width(spindle_label, 1, 0);
            lv_obj_set_style_border_color(spindle_label, WIZARD_COLOR_DIM_GRAY, 0);
            lv_obj_set_style_text_align(spindle_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(spindle_label, LV_ALIGN_CENTER, 0, 0);
        }
    }

    /* ── MIDDLE: Spindle ── */
    lv_obj_t *sc = wizard_create_card(s_wiz.content, 220, 260);
    lv_obj_align(sc, LV_ALIGN_TOP_LEFT, 354, 8);
    lv_obj_clear_flag(sc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *st = wizard_create_label(sc, "Spindle", 18);
    lv_obj_align(st, LV_ALIGN_TOP_LEFT, 12, 6);

    lv_obj_t *slbl = wizard_create_label(sc, "Speed (RPM)", 14);
    lv_obj_set_style_text_color(slbl, WIZARD_COLOR_GRAY, 0);
    lv_obj_align(slbl, LV_ALIGN_TOP_LEFT, 12, 36);

    s_wiz.spindle_speed_display = lv_label_create(sc);
    set_label_textf(s_wiz.spindle_speed_display, "%d", s_wiz.spindle_speed);
    lv_obj_set_style_text_color(s_wiz.spindle_speed_display, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wiz.spindle_speed_display, &lv_font_montserrat_22, 0);
    lv_obj_set_size(s_wiz.spindle_speed_display, lv_pct(80), 40);
    lv_obj_align(s_wiz.spindle_speed_display, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_wiz.spindle_speed_display, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_radius(s_wiz.spindle_speed_display, 8, 0);
    lv_obj_set_style_border_width(s_wiz.spindle_speed_display, 1, 0);
    lv_obj_set_style_border_color(s_wiz.spindle_speed_display, WIZARD_COLOR_DIM_GRAY, 0);
    lv_obj_set_style_text_align(s_wiz.spindle_speed_display, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_wiz.spindle_speed_display, 8, 0);
    lv_obj_add_flag(s_wiz.spindle_speed_display, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wiz.spindle_speed_display, event_spindle_speed_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *son = lv_button_create(sc);
    lv_obj_set_size(son, lv_pct(80), 38);
    lv_obj_set_style_bg_color(son, WIZARD_COLOR_GREEN_ACCENT, 0);
    lv_obj_set_style_radius(son, 8, 0);
    lv_obj_set_style_border_width(son, 0, 0);
    lv_obj_align(son, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_event_cb(son, event_spindle_on, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sol = lv_label_create(son);
    lv_label_set_text(sol, "On");
    lv_obj_set_style_text_font(sol, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sol, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(sol);

    lv_obj_t *soff = lv_button_create(sc);
    lv_obj_set_size(soff, lv_pct(80), 38);
    lv_obj_set_style_bg_color(soff, WIZARD_COLOR_RED_DANGER, 0);
    lv_obj_set_style_radius(soff, 8, 0);
    lv_obj_set_style_border_width(soff, 0, 0);
    lv_obj_align(soff, LV_ALIGN_TOP_MID, 0, 158);
    lv_obj_add_event_cb(soff, event_spindle_off, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sfl = lv_label_create(soff);
    lv_label_set_text(sfl, "Off");
    lv_obj_set_style_text_font(sfl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sfl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(sfl);

    if (s_wiz.wizard_flow) {
        bool zeroed = s_wiz.origin_set;
        build_bottom_bar("Back", event_back_to_origin, "Next", event_jog_next, zeroed);
        if (!zeroed) {
            lv_obj_set_style_bg_color(s_wiz.bottom_next_btn, WIZARD_COLOR_DIM_GRAY, 0);
            lv_obj_add_state(s_wiz.bottom_next_btn, LV_STATE_DISABLED);
        }
    } else {
        build_bottom_bar("Back", event_back_to_home, NULL, NULL, false);
    }

    /* ── RIGHT: Jog ── */
    lv_obj_t *jog_area = lv_obj_create(s_wiz.content);
    lv_obj_set_size(jog_area, 440, 270);
    lv_obj_set_style_bg_opa(jog_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(jog_area, 0, 0);
    lv_obj_clear_flag(jog_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(jog_area, LV_ALIGN_TOP_RIGHT, -2, 8);

    /* XY rose with larger buttons */
    lv_coord_t bs = 70;
    lv_coord_t gap = 10;
    lv_coord_t x_off = 8;
    lv_coord_t y_off = 4;
    lv_color_t jog_gray = WIZARD_COLOR_DIM_GRAY;
    lv_color_t jog_blue = WIZARD_COLOR_BLUE_ACCENT;

    create_jog_btn(jog_area, x_off + bs + gap, y_off, bs, "Y+", jog_hold_cb, (void *)JOG_ACTION_Y_PLUS, jog_blue);
    create_jog_btn(jog_area, x_off + bs + gap, y_off + 2*(bs+gap), bs, "Y-", jog_hold_cb, (void *)JOG_ACTION_Y_MINUS, jog_blue);
    create_jog_btn(jog_area, x_off, y_off + bs + gap, bs, "X-", jog_hold_cb, (void *)JOG_ACTION_X_MINUS, jog_blue);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off + bs + gap, bs, "X+", jog_hold_cb, (void *)JOG_ACTION_X_PLUS, jog_blue);

    create_jog_btn(jog_area, x_off, y_off, bs, "NW", event_jog_diag, (void *)"X- Y-", jog_gray);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off, bs, "NE", event_jog_diag, (void *)"X+ Y-", jog_gray);
    create_jog_btn(jog_area, x_off, y_off + 2*(bs+gap), bs, "SW", event_jog_diag, (void *)"X- Y+", jog_gray);
    create_jog_btn(jog_area, x_off + 2*(bs+gap), y_off + 2*(bs+gap), bs, "SE", event_jog_diag, (void *)"X+ Y+", jog_gray);

    lv_obj_t *stop_btn = lv_button_create(jog_area);
    lv_obj_set_size(stop_btn, bs, bs);
    lv_obj_set_pos(stop_btn, x_off + bs + gap, y_off + bs + gap);
    lv_obj_set_style_bg_color(stop_btn, WIZARD_COLOR_RED_DANGER, 0);
    lv_obj_set_style_radius(stop_btn, bs / 4, 0);
    lv_obj_set_style_border_width(stop_btn, 0, 0);
    lv_obj_add_event_cb(stop_btn, event_jog_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(stop_btn);
    lv_label_set_text(sl, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(sl, WIZARD_COLOR_WHITE, 0);
    lv_obj_center(sl);

    /* Z and A jog buttons next to jog rose (2x2 grid on the right) */
    lv_coord_t za_x = x_off + 3*bs + 2*gap + 14;
    lv_coord_t za_y = y_off;

    create_jog_btn(jog_area, za_x, za_y, bs, "Z+", jog_hold_cb, (void *)JOG_ACTION_Z_PLUS, jog_blue);
    create_jog_btn(jog_area, za_x + bs + gap, za_y, bs, "A+", jog_hold_cb, (void *)JOG_ACTION_A_PLUS, jog_blue);
    create_jog_btn(jog_area, za_x, za_y + bs + gap, bs, "Z-", jog_hold_cb, (void *)JOG_ACTION_Z_MINUS, jog_blue);
    create_jog_btn(jog_area, za_x + bs + gap, za_y + bs + gap, bs, "A-", jog_hold_cb, (void *)JOG_ACTION_A_MINUS, jog_blue);

    /* Action buttons below jog */
    typedef struct { const char *label; lv_color_t color; lv_event_cb_t cb; } action_btn_t;
    action_btn_t btns[] = {
        {LV_SYMBOL_REFRESH " Unlock",    WIZARD_COLOR_ORANGE,      event_unlock},
        {LV_SYMBOL_HOME "  Home All",    WIZARD_COLOR_BLUE_ACCENT, event_manual_home_all},
        {LV_SYMBOL_REFRESH "  Probe",    WIZARD_COLOR_BLUE_ACCENT, event_manual_probe},
        {LV_SYMBOL_GPS "  Set Zero",     WIZARD_COLOR_PURPLE,      event_set_zero},
    };
    lv_coord_t btn_w = 260;
    lv_coord_t btn_h = 44;
    lv_coord_t btn_gap = 6;
    lv_coord_t btn_x = LV_HOR_RES - 440 - 2 + 90;
    lv_coord_t btn_y = 286;
    for (int i = 0; i < 4; i++) {
        if (!s_wiz.wizard_flow && i == 0) {
            /* all buttons visible in standalone mode */
        } else if (s_wiz.wizard_flow && i != 3) {
            continue;
        }
        lv_obj_t *btn = lv_button_create(s_wiz.content);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_style_bg_color(btn, btns[i].color, 0);
        lv_obj_set_style_bg_color(btn, lv_color_darken(btns[i].color, 30), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, btn_x, btn_y);
        lv_obj_add_event_cb(btn, btns[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btns[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, WIZARD_COLOR_WHITE, 0);
        lv_obj_center(lbl);
        btn_y += btn_h + btn_gap;
    }

    /* ── Spindle speed modal (shared on screen) ── */
    s_wiz.spindle_modal_bg = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.spindle_modal_bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_wiz.spindle_modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_wiz.spindle_modal_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_wiz.spindle_modal_bg, 0, 0);
    lv_obj_clear_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_wiz.spindle_modal_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wiz.spindle_modal_bg, event_spindle_speed_done, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sp_card = wizard_create_card(s_wiz.spindle_modal_bg, 420, 480);
    lv_obj_center(sp_card);
    lv_obj_set_style_pad_all(sp_card, 12, 0);

    s_wiz.spindle_speed_title = lv_label_create(sp_card);
    lv_label_set_text(s_wiz.spindle_speed_title, "Set Spindle Speed");
    lv_obj_set_style_text_font(s_wiz.spindle_speed_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wiz.spindle_speed_title, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(s_wiz.spindle_speed_title, LV_ALIGN_TOP_MID, 0, 8);

    s_wiz.spindle_speed_ta = lv_textarea_create(sp_card);
    lv_textarea_set_one_line(s_wiz.spindle_speed_ta, true);
    lv_textarea_set_text(s_wiz.spindle_speed_ta, "12000");
    lv_obj_set_size(s_wiz.spindle_speed_ta, lv_pct(100), 52);
    lv_obj_set_style_bg_color(s_wiz.spindle_speed_ta, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_text_color(s_wiz.spindle_speed_ta, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wiz.spindle_speed_ta, &lv_font_montserrat_28, 0);
    lv_obj_set_style_radius(s_wiz.spindle_speed_ta, 8, 0);
    lv_obj_set_style_border_width(s_wiz.spindle_speed_ta, 2, 0);
    lv_obj_set_style_border_color(s_wiz.spindle_speed_ta, WIZARD_COLOR_DIM_GRAY, 0);
    lv_obj_set_style_text_align(s_wiz.spindle_speed_ta, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_right(s_wiz.spindle_speed_ta, 12, 0);
    lv_obj_align(s_wiz.spindle_speed_ta, LV_ALIGN_TOP_MID, 0, 44);

    s_wiz.spindle_kb = lv_keyboard_create(sp_card);
    lv_obj_set_size(s_wiz.spindle_kb, lv_pct(100), 364);
    lv_obj_align(s_wiz.spindle_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_wiz.spindle_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_wiz.spindle_kb, s_wiz.spindle_speed_ta);
    lv_obj_set_style_bg_color(s_wiz.spindle_kb, WIZARD_COLOR_BG, 0);
    lv_obj_set_style_bg_color(s_wiz.spindle_kb, WIZARD_COLOR_CARD_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_wiz.spindle_kb, WIZARD_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_wiz.spindle_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_wiz.spindle_kb, WIZARD_COLOR_LIGHT_GRAY, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_wiz.spindle_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(s_wiz.spindle_kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(s_wiz.spindle_kb, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_wiz.spindle_kb, event_spindle_speed_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wiz.spindle_kb, event_spindle_speed_done, LV_EVENT_CANCEL, NULL);

    /* ── TARGET POSITION MODAL ── */
    s_wiz.target_modal_bg = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.target_modal_bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_wiz.target_modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_wiz.target_modal_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_wiz.target_modal_bg, 0, 0);
    lv_obj_clear_flag(s_wiz.target_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wiz.target_modal_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_wiz.target_modal_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wiz.target_modal_bg, event_target_done, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tg_card = wizard_create_card(s_wiz.target_modal_bg, 400, 340);
    lv_obj_center(tg_card);
    lv_obj_set_style_pad_all(tg_card, 12, 0);

    s_wiz.target_title = lv_label_create(tg_card);
    lv_label_set_text(s_wiz.target_title, "Set Target Position");
    lv_obj_set_style_text_font(s_wiz.target_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wiz.target_title, WIZARD_COLOR_TEXT, 0);
    lv_obj_align(s_wiz.target_title, LV_ALIGN_TOP_MID, 0, 8);

    s_wiz.target_ta = lv_textarea_create(tg_card);
    lv_textarea_set_one_line(s_wiz.target_ta, true);
    lv_textarea_set_text(s_wiz.target_ta, "0.000");
    lv_obj_set_size(s_wiz.target_ta, lv_pct(100), 52);
    lv_obj_set_style_bg_color(s_wiz.target_ta, WIZARD_COLOR_INPUT_BG, 0);
    lv_obj_set_style_text_color(s_wiz.target_ta, WIZARD_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wiz.target_ta, &lv_font_montserrat_28, 0);
    lv_obj_set_style_radius(s_wiz.target_ta, 8, 0);
    lv_obj_set_style_border_width(s_wiz.target_ta, 2, 0);
    lv_obj_set_style_border_color(s_wiz.target_ta, WIZARD_COLOR_DIM_GRAY, 0);
    lv_obj_set_style_text_align(s_wiz.target_ta, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_right(s_wiz.target_ta, 12, 0);
    lv_obj_align(s_wiz.target_ta, LV_ALIGN_TOP_MID, 0, 44);

    s_wiz.target_kb = lv_keyboard_create(tg_card);
    lv_obj_set_size(s_wiz.target_kb, lv_pct(100), 220);
    lv_obj_align(s_wiz.target_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_wiz.target_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_style_bg_color(s_wiz.target_kb, WIZARD_COLOR_BG, 0);
    lv_obj_set_style_bg_color(s_wiz.target_kb, WIZARD_COLOR_CARD_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_wiz.target_kb, WIZARD_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_wiz.target_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_wiz.target_kb, WIZARD_COLOR_LIGHT_GRAY, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_wiz.target_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(s_wiz.target_kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(s_wiz.target_kb, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_wiz.target_kb, event_target_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wiz.target_kb, event_target_done, LV_EVENT_CANCEL, NULL);
}

/* ─── TOOLS & MACROS ─── */
static void event_dismiss_msgbox(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_target_obj(e);
    lv_obj_del(mbox);
}

static void event_tool_placeholder(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *mbox = lv_msgbox_create(s_wiz.screen);
    lv_msgbox_add_title(mbox, "Coming Soon");
    lv_msgbox_add_text(mbox, "This tool will be available in a future update.");
    lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_add_event_cb(mbox, event_dismiss_msgbox, LV_EVENT_CLICKED, NULL);
    lv_obj_center(mbox);
}

static void build_tools(void)
{
    clear_content();
    s_wiz.state = WIZARD_TOOLS;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Tools & Macros");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *card = wizard_create_card(s_wiz.content, 600, 460);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);

    const char *tool_items[] = {
        "Surfacing",
        "Rotary Surfacing",
        "Movement Tuning",
        "XY Squaring",
        "SD Card Manager",
    };
    int num_tools = sizeof(tool_items) / sizeof(tool_items[0]);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_add_style(list, wizard_get_scrollbar_style(), 0);

    for (int i = 0; i < num_tools; i++) {
        lv_obj_t *btn = lv_button_create(list);
        lv_obj_set_size(btn, lv_pct(100), 52);
        lv_obj_set_style_bg_color(btn, WIZARD_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_color(btn, WIZARD_COLOR_INPUT_BG, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, WIZARD_COLOR_LIGHT_GRAY, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_pad_left(btn, 16, 0);
        lv_obj_add_event_cb(btn, event_tool_placeholder, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, tool_items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, WIZARD_COLOR_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    }

    build_bottom_bar("Back", event_back_to_home, NULL, NULL, false);
}

/* ─── MACHINE SETUP ─── */
static void event_setup_wifi(lv_event_t *e)
{
    LV_UNUSED(e);
    build_settings_modal();
}

static void build_setup(void)
{
    clear_content();
    s_wiz.state = WIZARD_SETUP;

    s_wiz.content = lv_obj_create(s_wiz.screen);
    lv_obj_set_size(s_wiz.content, lv_pct(100), LV_VER_RES - TOP_BAR_H);
    lv_obj_add_style(s_wiz.content, wizard_get_container_style(), 0);
    lv_obj_clear_flag(s_wiz.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wiz.content, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);

    lv_obj_t *title = wizard_create_title_label(s_wiz.content, "Machine Setup");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *card = wizard_create_card(s_wiz.content, 600, 460);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);

    const char *setup_items[] = {
        "Setup Machine Profile",
        "Setup WiFi",
        "Setup Accessories",
        "Maintenance",
    };
    lv_event_cb_t setup_cbs[] = {
        event_tool_placeholder,
        event_setup_wifi,
        event_tool_placeholder,
        event_tool_placeholder,
    };
    int num_items = sizeof(setup_items) / sizeof(setup_items[0]);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_add_style(list, wizard_get_scrollbar_style(), 0);

    for (int i = 0; i < num_items; i++) {
        lv_obj_t *btn = lv_button_create(list);
        lv_obj_set_size(btn, lv_pct(100), 52);
        lv_obj_set_style_bg_color(btn, WIZARD_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_color(btn, WIZARD_COLOR_INPUT_BG, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, WIZARD_COLOR_LIGHT_GRAY, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_pad_left(btn, 16, 0);
        lv_obj_add_event_cb(btn, setup_cbs[i], LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, setup_items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, WIZARD_COLOR_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    }

    build_bottom_bar("Back", event_back_to_home, NULL, NULL, false);
}

/* ─── NAVIGATION ─── */
static void navigate_to(wizard_state_t state)
{
    if (s_wiz.jog_repeat_timer) {
        lv_timer_delete(s_wiz.jog_repeat_timer);
        s_wiz.jog_repeat_timer = NULL;
    }

    if (state != WIZARD_MANUAL) {
        s_wiz.wizard_flow = false;
    }

    switch (state) {
        case WIZARD_SPLASH:       build_splash(); break;
        case WIZARD_HOME:         build_home(); break;
        case WIZARD_FILE_SELECT:  build_file_select(); break;
        case WIZARD_ORIGIN_CHOICE: build_origin_choice(); break;
        case WIZARD_JOG:          build_jog(); break;
        case WIZARD_PROBE:        build_probe(); break;
        case WIZARD_TOOL_SELECT:  build_tool_select(); break;
        case WIZARD_READY:        build_ready(); break;
        case WIZARD_RUNNING:      build_running(); break;
        case WIZARD_COMPLETE:     build_complete(); break;
        case WIZARD_MANUAL:       build_manual(); break;
        case WIZARD_TOOLS:        build_tools(); break;
        case WIZARD_SETUP:        build_setup(); break;
    }
}

/* ─── PUBLIC ENTRY ─── */
void cnc_wizard_create(lv_display_t *disp)
{
    memset(&s_wiz, 0, sizeof(s_wiz));
    s_wiz.disp = disp;
    s_wiz.feed_override_pct = 100;
    s_wiz.speed_override_pct = 100;
    s_wiz.spindle_speed = 12000;
    s_wiz.target_x = 0; s_wiz.target_y = 0; s_wiz.target_z = 0; s_wiz.target_a = 0;

    persist_load_and_apply();

    cnc_link_init();
    bsp_sdcard_mount();

    s_wiz.screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_wiz.screen, WIZARD_COLOR_DARK_BG, 0);
    lv_obj_clear_flag(s_wiz.screen, LV_OBJ_FLAG_SCROLLABLE);

    build_top_bar();

    s_wiz.refresh_timer = lv_timer_create(network_refresh_timer_cb, 500, NULL);

    navigate_to(WIZARD_SPLASH);
}
