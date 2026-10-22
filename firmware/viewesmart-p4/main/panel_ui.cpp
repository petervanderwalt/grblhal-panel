#include "panel_ui.h"
#include "lvgl.h"
#include "nvs.h"
#include "panel_runtime.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
using namespace panel;
constexpr uint32_t BG = 0x101923, CARD = 0x192837, EDGE = 0x2C4053, TEXT = 0xEAF2F8, MUTED = 0x9AAFC0,
                   BLUE = 0x3FC6F4, RED = 0xF16B73, GREEN = 0x62D6AC, AMBER = 0xF5C768;
struct Preferences {
    uint32_t magic = 0x50414E31;
    ProbeSettings probe;
    float tx = 37, rx = 38, baud = 115200, step = 1, jog_feed = 600;
};
static Preferences prefs;
static State state;
static int page = 0;
static lv_obj_t *root, *body, *status_text, *state_label, *protocol_label, *axis_work[3], *axis_machine[3],
    *axis_card[3], *nav[5];
static lv_obj_t *pause_btn, *resume_btn, *home_btn, *stop_btn, *recover_btn;
static lv_obj_t *axis_zero[3];
static lv_obj_t *file_box, *file_title, *progress, *progress_text, *time_text, *line_text, *start_btn,
    *refresh_btn, *probe_start, *probe_status, *contact_btn, *wcs_label;
static lv_obj_t *modal = nullptr, *number = nullptr, *number_error = nullptr;
static char selected[160] = "", notice[192] = "";
static uint32_t revision = 0, notice_until = 0;
static lv_obj_t *gated[24];
static int gate_count;
static bool continuous = false, jog_held = false;
static lv_obj_t *jog_buttons[10];
static int jog_count;
static char home_axis;
struct Edit {
    float *value;
    float min, max;
    const char *title;
    bool integer = false;
};
static Edit editing;
static bool (*confirmation)() = nullptr;
static void build_page();
static void release_jog();
static void refresh(lv_timer_t *);
static uint32_t now() { return lv_tick_get(); }
static void save() {
    nvs_handle_t h;
    if (nvs_open("panel", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "prefs", &prefs, sizeof prefs);
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
        nvs_close(h);
    }
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
    lv_obj_set_style_border_color(o, lv_color_hex(EDGE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
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
                        uintptr_t data = 0, uint32_t color = EDGE) {
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 3, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font(16), 0);
    lv_obj_set_style_text_color(
        l, lv_color_hex((color == BLUE || color == GREEN || color == AMBER) ? BG : TEXT), 0);
    lv_obj_set_width(l, w - 10);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)data);
    return b;
}
static void enable(lv_obj_t *b, bool on) {
    if (!b)
        return;
    if (on)
        lv_obj_remove_state(b, LV_STATE_DISABLED);
    else
        lv_obj_add_state(b, LV_STATE_DISABLED);
    lv_obj_set_style_opa(b, on ? LV_OPA_COVER : LV_OPA_40, 0);
}
static void gate(lv_obj_t *b) {
    if (gate_count < 24)
        gated[gate_count++] = b;
}
static bool ready() {
    return state.online && state.status && state.identified && !state.busy && !state.alarm && !state.error &&
           !state.sd_active && !strcmp(state.mode, "Idle");
}
static void notify(const char *s) {
    snprintf(notice, sizeof notice, "%s", s);
    notice_until = now() + 5000;
}
static void result(bool ok) {
    if (!ok)
        notify("Action unavailable: check connection, machine state and setup.");
}
static void close_modal() {
    if (modal)
        lv_obj_delete(modal);
    modal = nullptr;
    number = nullptr;
    number_error = nullptr;
    confirmation = nullptr;
}
static lv_obj_t *open_modal(const char *title, int width = 600, int height = 320) {
    close_modal();
    modal = box(lv_layer_top(), 0, 0, 1024, 600, BG);
    lv_obj_set_style_bg_opa(modal, LV_OPA_80, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_t *card = box(modal, (1024 - width) / 2, (600 - height) / 2, width, height);
    label(card, title, 24, 22, width - 48, 22);
    lv_obj_move_foreground(stop_btn);
    return card;
}
static void confirmed(lv_event_t *) {
    auto fn = confirmation;
    close_modal();
    if (fn)
        result(fn());
}
static void cancelled(lv_event_t *) { close_modal(); }
static void confirm(const char *title, const char *text, bool (*fn)()) {
    lv_obj_t *c = open_modal(title);
    label(c, text, 24, 78, 552, 18, MUTED);
    confirmation = fn;
    button(c, "Cancel", 24, 242, 220, 54, cancelled);
    button(c, "Confirm", 264, 242, 312, 54, confirmed, 0, BLUE);
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
    if (!*s || *end || !isfinite(f) || f < editing.min || f > editing.max ||
        (editing.integer && floorf(f) != f)) {
        lv_label_set_text(number_error, "Outside range / check the number");
        return;
    }
    *editing.value = f;
    save();
    close_modal();
    build_page();
}
static void edit(Edit e) {
    editing = e;
    lv_obj_t *c = open_modal(e.title, 430, 540);
    number = lv_textarea_create(c);
    lv_obj_set_pos(number, 22, 70);
    lv_obj_set_size(number, 386, 54);
    lv_textarea_set_one_line(number, true);
    lv_textarea_set_max_length(number, 10);
    lv_textarea_set_accepted_chars(number, "0123456789.");
    char value[24];
    snprintf(value, sizeof value, e.integer ? "%.0f" : "%.3f", *e.value);
    lv_textarea_set_text(number, value);
    lv_obj_set_style_text_font(number, font(28), 0);
    lv_obj_set_style_bg_color(number, lv_color_hex(BG), 0);
    lv_obj_set_style_text_color(number, lv_color_hex(TEXT), 0);
    const char *keys[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "Back"};
    for (int i = 0; i < 12; i++) {
        lv_obj_t *b = button(c, keys[i], 22 + (i % 3) * 132, 138 + (i / 3) * 64, 122, 54, nullptr);
        lv_obj_add_event_cb(b, key, LV_EVENT_CLICKED, (void *)keys[i]);
    }
    button(c, "Cancel", 22, 404, 122, 50, cancelled);
    lv_obj_t *clear = button(c, "Clear", 154, 404, 122, 50, nullptr);
    lv_obj_add_event_cb(clear, key, LV_EVENT_CLICKED, (void *)"Clear");
    button(c, "Apply", 286, 404, 122, 50, apply_number, 0, BLUE);
    char limits[80];
    snprintf(limits, sizeof limits, "Range %.0f to %.0f", e.min, e.max);
    number_error = label(c, limits, 22, 476, 386, 14, MUTED);
}
static void edit_value(lv_event_t *e) {
    if (state.probing) {
        notify("Wait for the probe sequence to finish before editing geometry.");
        return;
    }
    switch ((uintptr_t)lv_event_get_user_data(e)) {
    case 0:
        edit({&prefs.step, .01f, 100, "Jog distance (mm)"});
        break;
    case 1:
        edit({&prefs.jog_feed, 1, 10000, "Jog feed (mm/min)"});
        break;
    case 2:
        edit({&prefs.probe.diameter, .1f, 50, "Tool diameter (mm)"});
        break;
    case 3:
        edit({&prefs.probe.x, .1f, 50, "Plate X offset (mm)"});
        break;
    case 4:
        edit({&prefs.probe.y, .1f, 50, "Plate Y offset (mm)"});
        break;
    case 5:
        edit({&prefs.probe.z, .1f, 50, "Plate Z thickness (mm)"});
        break;
    case 6:
        edit({&prefs.probe.feed, 25, 500, "Probe feed (mm/min)"});
        break;
    case 7:
        edit({&prefs.probe.inset, 1, 100, "Corner start inset (mm)"});
        break;
    case 8:
        edit({&prefs.tx, 0, 54, "UART TX pin", true});
        break;
    case 9:
        edit({&prefs.rx, 0, 54, "UART RX pin", true});
        break;
    case 10:
        edit({&prefs.baud, 9600, 921600, "UART baud rate", true});
        break;
    }
}
static void value_button(lv_obj_t *p, const char *title, float value, int x, int y, int w, int id) {
    char text[80];
    snprintf(text, sizeof text, id >= 8 ? "%s\n%.0f" : "%s\n%.3f", title, value);
    button(p, text, x, y, w, 66, edit_value, id);
}
static bool stop() { return runtime_action(Action::Stop); }
static bool home() { char axis[] = {home_axis, 0}; return runtime_action(Action::Home, home_axis ? axis : nullptr); }
static bool run() { return runtime_action(Action::Run, selected); }
static bool probe() { return runtime_probe(prefs.probe); }
static void home_click(lv_event_t *e) {
    home_axis = (uintptr_t)lv_event_get_user_data(e);
    char title[40];
    snprintf(title, sizeof title, home_axis ? "Home %c axis?" : "Home all axes?", home_axis);
    confirm(title, "Clear the machine travel before homing.", home);
}
static void stop_click(lv_event_t *) {
    close_modal();
    release_jog();
    result(stop());
}
static void pause_click(lv_event_t *) { result(runtime_action(Action::Pause)); }
static void resume_click(lv_event_t *) { result(runtime_action(Action::Resume)); }
static void release_jog() {
    if (jog_held) {
        jog_held = false;
        result(runtime_action(Action::JogCancel));
    }
}
static void jog_event(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        if (jog_held) return;
        const char *directions[] = {"-+0", "0+0", "++0", "-00", "+00", "--0", "0-0", "+-0", "00+", "00-"};
        bool ok = runtime_action(Action::Jog, directions[(uintptr_t)lv_event_get_user_data(e)],
                                 continuous ? 10000 : prefs.step, prefs.jog_feed);
        jog_held = ok && continuous;
        result(ok);
    } else if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
        release_jog();
    }
}
static void jog_mode(lv_event_t *e) {
    release_jog();
    continuous = (uintptr_t)lv_event_get_user_data(e);
    build_page();
}
static void list_click(lv_event_t *) { result(runtime_action(Action::List)); }
static void start_click(lv_event_t *) {
    char text[240];
    snprintf(text, sizeof text, "Run %s from controller SD?\nConfirm tool, work zero and clear travel.",
             selected);
    confirm("Start SD job?", text, run);
}
static void select_file(lv_event_t *e) {
    int i = (uintptr_t)lv_event_get_user_data(e);
    if (i < state.file_count && !state.files[i].directory) {
        snprintf(selected, sizeof selected, "%s", state.files[i].path);
        lv_label_set_text(file_title, selected);
        refresh(nullptr);
    }
}
static void files() {
    if (!file_box)
        return;
    lv_obj_clean(file_box);
    if (!state.file_count) {
        label(file_box, state.listing ? "Reading SD card..." : "Refresh to list controller SD files.", 18, 24,
              400, 16, MUTED);
        return;
    }
    for (int i = 0; i < state.file_count; i++) {
        char s[220];
        snprintf(s, sizeof s, "%s%s  /  %.1f KB", state.files[i].directory ? "[DIR] " : "",
                 state.files[i].path, state.files[i].size / 1024.f);
        lv_obj_t *b = button(file_box, s, 8, 8 + i * 53, 426, 46, select_file, i,
                             !strcmp(selected, state.files[i].path) ? 0x28516A : EDGE);
        enable(b, !state.files[i].directory);
    }
    revision = state.file_revision;
}
static void contact(lv_event_t *) { result(runtime_action(Action::Contact)); }
static void corner(lv_event_t *e) {
    if (state.probing)
        return;
    prefs.probe.corner = (uintptr_t)lv_event_get_user_data(e);
    save();
    build_page();
}
static void probe_click(lv_event_t *) {
    confirm("Start corner probe?",
            "Start above the plate hole, spindle OFF.\nThe active WCS XYZ offsets will change.\nKeep access "
            "to the physical emergency stop.",
            probe);
}
static int zero_axes;
static bool zero_confirm() { return runtime_action(Action::Zero, nullptr, zero_axes); }
static void zero_click(lv_event_t *e) {
    zero_axes = (uintptr_t)lv_event_get_user_data(e);
    confirm("Set work zero?",
            "Set the selected axes to zero in the active WCS.\nMachine coordinates are unchanged.",
            zero_confirm);
}
static void wcs(lv_event_t *e) {
    result(runtime_action(Action::Wcs, nullptr, 54 + (uintptr_t)lv_event_get_user_data(e)));
}
static void connect_click(lv_event_t *) {
    if (state.online)
        runtime_disconnect();
    else
        result(runtime_connect((int)prefs.tx, (int)prefs.rx, (int)prefs.baud));
}
static void demo_click(lv_event_t *) { runtime_demo(!runtime_is_demo()); }
static bool unlock() { return runtime_action(Action::Unlock); }
static bool acknowledge() { return runtime_action(Action::Acknowledge); }
static void recover(lv_event_t *) {
    if (state.alarm)
        confirm("Unlock alarm?",
                "Find and remove the alarm cause first.\n$X unlocks; it does not restore a lost position.",
                unlock);
    else if (state.error)
        confirm("Acknowledge command error?", state.message, acknowledge);
    else
        notify("No active alarm or command error.");
}
static void navigate(lv_event_t *e) {
    page = (uintptr_t)lv_event_get_user_data(e);
    build_page();
}
static void build_page() {
    release_jog();
    jog_count = 0;
    gate_count = 0;
    home_btn = nullptr;
    file_box = file_title = progress = progress_text = time_text = line_text = start_btn = refresh_btn =
        probe_start = probe_status = contact_btn = wcs_label = nullptr;
    lv_obj_clean(body);
    lv_obj_set_pos(body, page == 0 ? 16 : 90, 176);
    lv_obj_set_width(body, page == 0 ? 992 : 844);
    runtime_snapshot(state);
    for (int i = 0; i < 5; i++)
        lv_obj_set_style_bg_color(nav[i], lv_color_hex(i == page ? 0x25445B : BG), 0);
    if (page == 0) {
        label(body, "XY JOG", 8, 4, 310, 16, MUTED);
        label(body, "Z JOG", 844, 4, 148, 16, MUTED);
        button(body, "Incremental", 370, 0, 208, 64, jog_mode, 0, !continuous ? BLUE : EDGE);
        button(body, "Continuous", 594, 0, 208, 64, jog_mode, 1, continuous ? BLUE : EDGE);
        char s[40];
        snprintf(s, sizeof s, "Step %.2f mm", prefs.step);
        enable(button(body, s, 370, 76, 208, 64, edit_value, 0), !continuous);
        snprintf(s, sizeof s, "Feed %.0f mm/min", prefs.jog_feed);
        button(body, s, 594, 76, 208, 64, edit_value, 1);
        const char *names[] = {LV_SYMBOL_LEFT " " LV_SYMBOL_UP, "Y+", LV_SYMBOL_UP " " LV_SYMBOL_RIGHT,
                              "X-", "X+", LV_SYMBOL_LEFT " " LV_SYMBOL_DOWN, "Y-", LV_SYMBOL_DOWN " " LV_SYMBOL_RIGHT, "Z+", "Z-"};
        const int cells[] = {0,1,2,3,5,6,7,8};
        for (int i = 0; i < 10; i++) {
            int x = i < 8 ? (cells[i] % 3) * 114 : 844;
            int y = i < 8 ? 36 + (cells[i] / 3) * 96 : (i == 8 ? 36 : 188);
            lv_obj_t *b = button(body, names[i], x, y, i < 8 ? 104 : 148, i < 8 ? 88 : 128, nullptr);
            lv_obj_set_style_text_font(lv_obj_get_child(b, 0), font(28), 0);
            lv_obj_add_event_cb(b, jog_event, LV_EVENT_ALL, (void *)(uintptr_t)i);
            jog_buttons[jog_count++] = b;
        }
        label(body, "XY", 146, 162, 70, 22, MUTED);
        home_btn = button(body, LV_SYMBOL_HOME "  Home all", 370, 152, 208, 64, home_click);
        gate(button(body, "Zero XYZ", 594, 152, 208, 64, zero_click, 7));
        label(body, continuous ? "Hold to move\nRelease to stop" : "Tap once\nMove the set distance", 370, 244, 432, 20, MUTED);
    } else if (page == 1) {
        label(body, "Controller SD", 0, 0, 280, 22);
        refresh_btn = button(body, "Refresh", 316, 0, 128, 40, list_click, 0);
        file_box = box(body, 0, 50, 452, 246);
        lv_obj_add_flag(file_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(file_box, LV_DIR_VER);
        files();
        label(body, "SELECTED / RUNNING FILE", 480, 0, 364, 12, MUTED);
        file_title = label(body, *selected ? selected : "Select a file", 480, 28, 364, 18);
        progress_text = label(body, "-- %", 480, 92, 200, 32, BLUE);
        time_text = label(body, "00:00:00", 688, 108, 156, 18);
        progress = lv_bar_create(body);
        lv_obj_set_pos(progress, 480, 144);
        lv_obj_set_size(progress, 364, 12);
        lv_obj_set_style_bg_color(progress, lv_color_hex(EDGE), 0);
        lv_obj_set_style_bg_color(progress, lv_color_hex(BLUE), LV_PART_INDICATOR);
        line_text = label(body, "No active SD job", 480, 174, 364, 16, MUTED);
        start_btn = button(body, "Run selected file", 480, 236, 364, 60, start_click, 0, BLUE);
    } else if (page == 2) {
        label(body, "Corner probe", 0, 0, 350, 22);
        label(body, "OUTSIDE CORNER", 0, 42, 350, 12, MUTED);
        const char *names[] = {"Front left", "Front right", "Rear left", "Rear right"};
        for (int i = 0; i < 4; i++)
            button(body, names[i], i * 107, 62, 99, 48, corner, i, i == prefs.probe.corner ? 0x28516A : EDGE);
        value_button(body, "Tool diameter / mm", prefs.probe.diameter, 0, 132, 212, 2);
        value_button(body, "Feed / mm/min", prefs.probe.feed, 228, 132, 200, 6);
        char text[180];
        snprintf(text, sizeof text,
                 "Plate X %.1f / Y %.1f / Z %.1f mm\nStart above plate hole. Spindle OFF.\nActive G%d "
                 "offsets will change.",
                 prefs.probe.x, prefs.probe.y, prefs.probe.z, state.wcs);
        label(body, text, 0, 224, 440, 16, MUTED);
        probe_status = label(body, "Contact not verified", 474, 42, 370, 18, AMBER);
        contact_btn = button(body, "Test contact + release", 474, 90, 370, 66, contact, 0);
        probe_start = button(body, "Probe corner", 474, 232, 370, 64, probe_click, 0, BLUE);
        label(body,
              "1  Touch tool to plate, then release.\n2  Enter tool diameter.\n3  Position over hole and "
              "start.",
              474, 170, 370, 14, MUTED);
    } else if (page == 3) {
        label(body, "Work zero & coordinates", 0, 0, 600, 22);
        wcs_label = label(body, "", 0, 42, 844, 16, MUTED);
        for (int i = 0; i < 6; i++) {
            char s[12];
            snprintf(s, sizeof s, "G%d", 54 + i);
            gate(button(body, s, i * 142, 80, 132, 54, wcs, i));
        }
        gate(button(body, "Zero X", 0, 164, 198, 72, zero_click, 1));
        gate(button(body, "Zero Y", 214, 164, 198, 72, zero_click, 2));
        gate(button(body, "Zero Z", 428, 164, 198, 72, zero_click, 4));
        gate(button(body, "Zero XYZ", 642, 164, 198, 72, zero_click, 7, 0x28516A));
        for (int i = 0; i < 4; i++) {
            char s[20];
            snprintf(s, sizeof s, i == 3 ? "Home all" : "Home %c", "XYZ"[i < 3 ? i : 0]);
            gate(button(body, s, i * 214, 248, 198, 64, home_click, i == 3 ? 0 : "XYZ"[i]));
        }
    } else {
        label(body, "Probe defaults", 0, 0, 420, 22);
        value_button(body, "Plate X / mm", prefs.probe.x, 0, 48, 132, 3);
        value_button(body, "Plate Y / mm", prefs.probe.y, 144, 48, 132, 4);
        value_button(body, "Plate Z / mm", prefs.probe.z, 288, 48, 132, 5);
        value_button(body, "Feed / mm/min", prefs.probe.feed, 0, 132, 204, 6);
        value_button(body, "Start inset / mm", prefs.probe.inset, 220, 132, 200, 7);
        label(body, "Saved on this display. Controller settings\nare read for state; never auto-rewritten.",
              0, 234, 430, 16, MUTED);
        label(body, "Controller UART", 474, 0, 370, 22);
        value_button(body, "TX pin", prefs.tx, 474, 48, 108, 8);
        value_button(body, "RX pin", prefs.rx, 594, 48, 108, 9);
        value_button(body, "Baud", prefs.baud, 714, 48, 130, 10);
        button(body, state.online ? "Disconnect" : "Connect controller", 474, 134, 370, 62, connect_click, 0,
               BLUE);
        button(body, runtime_is_demo() ? "Exit preview" : "Preview UI / UART off", 474, 230, 370, 62,
               demo_click);
    }
    refresh(nullptr);
}
static void refresh(lv_timer_t *) {
    runtime_snapshot(state);
    bool on = ready();
    char s[256];
    snprintf(s, sizeof s, "%s%s", runtime_is_demo() ? "PREVIEW / " : "", state.mode);
    lv_label_set_text(state_label, s);
    lv_obj_set_style_text_color(state_label,
                                lv_color_hex(state.alarm || state.error        ? RED
                                             : !strncmp(state.mode, "Hold", 4) ? AMBER
                                             : state.status                    ? GREEN
                                                                               : MUTED),
                                0);
    snprintf(s, sizeof s, "G%d  /  mm  /  %s", state.wcs,
             state.identified ? (state.extended ? "RT 0x80" : "Legacy status") : "UART offline");
    lv_label_set_text(protocol_label, s);
    uint32_t colors[] = {RED, GREEN, BLUE};
    for (int i = 0; i < 3; i++) {
        if (state.coordinates && state.wco_known) {
            snprintf(s, sizeof s, "% .3f", state.work[i]);
            lv_label_set_text(axis_work[i], s);
            snprintf(s, sizeof s, "M  % .3f", state.machine[i]);
            lv_label_set_text(axis_machine[i], s);
        } else {
            lv_label_set_text(axis_work[i], "--.---");
            lv_label_set_text(axis_machine[i], "M  --.---");
        }
        lv_obj_set_style_text_color(axis_work[i], lv_color_hex(state.status ? TEXT : MUTED), 0);
        lv_obj_set_style_border_color(axis_card[i], lv_color_hex(state.status ? colors[i] : EDGE), 0);
    }
    const char *msg = now() < notice_until ? notice : state.message;
    if (state.alarm || state.error)
        snprintf(s, sizeof s, "%s %d / %s", state.alarm ? "ALARM" : "ERROR",
                 state.alarm ? state.alarm : state.error, msg);
    else
        snprintf(s, sizeof s, "%s", msg);
    lv_label_set_text(status_text, s);
    lv_obj_set_style_text_color(status_text, lv_color_hex(state.alarm || state.error ? RED : MUTED), 0);
    for (int i = 0; i < gate_count; i++)
        enable(gated[i], on);
    for (int i = 0; i < 3; i++)
        enable(axis_zero[i], on);
    for (int i = 0; i < jog_count; i++)
        enable(jog_buttons[i], on || jog_held);
    enable(home_btn, on || (state.online && state.identified && state.status && !state.busy &&
                            !strncmp(state.mode, "Alarm", 5)));
    enable(pause_btn, state.online && state.status && !strncmp(state.mode, "Run", 3));
    enable(resume_btn, state.online && state.status && !strcmp(state.mode, "Hold:0"));
    enable(stop_btn, state.online || state.identified);
    enable(recover_btn, state.online && (state.alarm || state.error));
    if (page == 1 && file_box) {
        if (revision != state.file_revision)
            files();
        enable(refresh_btn, on);
        enable(start_btn, on && *selected);
        if (state.sd_active)
            lv_label_set_text(file_title, state.filename);
        snprintf(s, sizeof s, "%.1f %%", state.percent);
        lv_label_set_text(progress_text, s);
        lv_bar_set_value(progress, (int)state.percent, LV_ANIM_OFF);
        snprintf(s, sizeof s, "%02lu:%02lu:%02lu", (unsigned long)state.elapsed / 3600,
                 (unsigned long)state.elapsed / 60 % 60, (unsigned long)state.elapsed % 60);
        lv_label_set_text(time_text, s);
        snprintf(s, sizeof s, "%s  /  Line %d",
                 state.sd_active        ? state.mode
                 : state.percent >= 100 ? "Completed"
                                        : "No active SD job",
                 state.line);
        lv_label_set_text(line_text, s);
    }
    if (page == 2 && probe_start) {
        if (state.probing) {
            snprintf(s, sizeof s, "Probing axis %d / 3", state.probe_stage < 3 ? state.probe_stage + 1 : 3);
            lv_label_set_text(probe_status, s);
        } else
            lv_label_set_text(probe_status, state.probe           ? "CONTACT / triggered"
                                            : state.contact_ready ? "Contact verified"
                                            : state.probe_known   ? "Probe released / test required"
                                                                  : "Waiting for probe feedback");
        lv_obj_set_style_text_color(probe_status, lv_color_hex(state.contact_ready ? GREEN : AMBER), 0);
        enable(contact_btn, on && state.probe_known);
        enable(probe_start, on && state.contact_ready && !state.probe && state.spindle == 0 &&
                                prefs.probe.diameter > 0 && state.modal_known && state.wco_known);
    }
    if (wcs_label) {
        snprintf(s, sizeof s, "Active G%d  /  Work = Machine - Work offset", state.wcs);
        lv_label_set_text(wcs_label, s);
    }
}
} // namespace
extern "C" void panel_ui_create() {
    runtime_init();
    load();
    root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    label(root, "grblHAL", 16, 20, 148, 22);
    state_label = label(root, "Disconnected", 174, 22, 352, 20, MUTED);
    protocol_label = label(root, "UART offline", 690, 504, 318, 12, MUTED);
    lv_obj_set_style_text_align(protocol_label, LV_TEXT_ALIGN_RIGHT, 0);
    const char *tabs[] = {"Jog", "Files", "Probe", "Zero / Home", "Settings"};
    for (int i = 0; i < 5; i++)
        nav[i] = button(root, tabs[i], 16 + i * 154, 528, 144, 64, navigate, i, EDGE);
    const char *axes[] = {"X", "Y", "Z"};
    uint32_t colors[] = {RED, GREEN, BLUE};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = box(root, 16 + i * 336, 66, 320, 98);
        axis_card[i] = c;
        label(c, axes[i], 16, 16, 30, 20, colors[i]);
        axis_work[i] = label(c, "--.---", 66, 10, 238, 32);
        lv_obj_set_style_text_align(axis_work[i], LV_TEXT_ALIGN_RIGHT, 0);
        axis_machine[i] = label(c, "M  --.---", 16, 65, 188, 14, MUTED);
        lv_obj_set_style_text_align(axis_machine[i], LV_TEXT_ALIGN_RIGHT, 0);
        char s[16];
        snprintf(s, sizeof s, "Zero %c", "XYZ"[i]);
        axis_zero[i] = button(c, s, 224, 48, 84, 44, zero_click, 1 << i);
    }
    body = box(root, 16, 176, 992, 328, BG);
    lv_obj_set_style_border_width(body, 0, 0);
    status_text = label(root, "Connect controller in Settings", 16, 504, 654, 14, MUTED);
    lv_obj_set_height(status_text, 20);
    lv_label_set_long_mode(status_text, LV_LABEL_LONG_DOT);
    pause_btn = button(root, "Pause", 692, 8, 150, 48, pause_click, 0, 0x6B542D);
    resume_btn = button(root, "Resume", 858, 8, 150, 48, resume_click, 0, 0x225B4A);
    stop_btn = button(lv_layer_top(), "STOP", 790, 528, 218, 64, nullptr, 0, 0x8A353F);
    lv_obj_add_event_cb(stop_btn, stop_click, LV_EVENT_PRESSED, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(stop_btn, 0), font(22), 0);
    recover_btn = button(root, "Recover", 546, 8, 130, 48, recover);
    build_page();
    lv_timer_create(refresh, 100, nullptr);
}
extern "C" void panel_ui_settings(char *buffer, size_t size) {
    snprintf(buffer, size,
             "{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"diameter\":%.3f,\"feed\":%.3f,\"tx\":%.0f,\"rx\":%.0f,"
             "\"baud\":%.0f}",
             prefs.probe.x, prefs.probe.y, prefs.probe.z, prefs.probe.diameter, prefs.probe.feed, prefs.tx,
             prefs.rx, prefs.baud);
}
