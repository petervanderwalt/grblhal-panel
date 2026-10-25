// Included inside panel_ui.cpp's private namespace to reuse the pendant's controls.
static lv_obj_t *act_position[4], *act_travel[4], *act_slider[4], *act_home[4], *act_move[4][3], *act_stop[4];
static lv_obj_t *act_fields[6], *act_keyboard, *act_style, *act_config_error;
static lv_obj_t *act_arc, *act_units[3];
static bool act_edit_rotary;
static int act_edit_axis, act_home_axis;
static lv_obj_t *act_go[4], *act_speed_text[4];
static int act_speed_percent[4] = {50, 50, 50, 50};

static void actuator_units() {
    const char *names[] = {"First position", "Second position", "Speed"};
    for (int i = 0; i < 3; i++) {
        char text[64];
        snprintf(text, sizeof text, "%s (%s)", names[i], act_edit_rotary ? (i == 2 ? "deg/min" : "deg, machine") :
                 i == 2 ? (inches ? "in/min" : "mm/min") : (inches ? "in, machine" : "mm, machine"));
        lv_label_set_text(act_units[i], text);
    }
}
static void actuator_type(lv_event_t *e) {
    act_edit_rotary = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1;
    actuator_units();
}

static void application_changed(lv_event_t *e) {
    runtime_snapshot(state);
    if (state.online && (state.busy || state.sd_active || state.cycle_axis >= 0 ||
        (strcmp(state.mode, "Idle") && strncmp(state.mode, "Alarm", 5)))) {
        auto selector = (lv_obj_t *)lv_event_get_target(e);
        lv_buttonmatrix_clear_button_ctrl_all(selector, LV_BUTTONMATRIX_CTRL_CHECKED);
        lv_buttonmatrix_set_button_ctrl(selector, actuator_mode, LV_BUTTONMATRIX_CTRL_CHECKED);
        notify("Stop motion before changing mode.");
        return;
    }
    actuator_mode = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1;
    save();
    // Let the selection event finish before replacing its parent.
    lv_async_call([](void *) { build_page(); }, nullptr);
}
static void actuator_focus(lv_event_t *e) {
    auto field = (lv_obj_t *)lv_event_get_target(e);
    lv_keyboard_set_textarea(act_keyboard, field);
    lv_keyboard_set_mode(act_keyboard, (uintptr_t)lv_event_get_user_data(e) >= 3 ?
                         LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
}
static void actuator_config_save(lv_event_t *) {
    ActuatorConfig a = actuator_prefs.axes[act_edit_axis];
    char *names[] = {a.name, a.first, a.second};
    const size_t sizes[] = {sizeof a.name, sizeof a.first, sizeof a.second};
    for (int i = 0; i < 3; i++) {
        const char *s = lv_textarea_get_text(act_fields[i]);
        if (!*s || strlen(s) >= sizes[i]) {
            lv_label_set_text(act_config_error, "Check label length");
            return;
        }
        snprintf(names[i], sizes[i], "%s", s);
    }
    float *values[] = {&a.end1, &a.end2, &a.feed};
    for (int i = 0; i < 3; i++) {
        const char *s = lv_textarea_get_text(act_fields[3 + i]);
        char unchanged[32];
        float scale = act_edit_rotary ? 1 : display_scale(inches);
        snprintf(unchanged, sizeof unchanged, "%.3f", *values[i] / scale);
        if ((act_edit_axis != 3 || act_edit_rotary == a_rotary) && !strcmp(s, unchanged))
            continue; // Editing labels must not round an unchanged inch-converted endpoint.
        char *end;
        float value = strtof(s, &end) * scale;
        if (!*s || *end || !isfinite(value) || fabsf(value) > (i == 2 ? 1000000 : 100000) ||
            (i == 2 && value < .1f)) {
            lv_label_set_text(act_config_error, "Check position / speed");
            return;
        }
        *values[i] = value;
    }
    int style = -1;
    for (int i = 0; i < 4; i++)
        if (lv_buttonmatrix_has_button_ctrl(act_style, i, LV_BUTTONMATRIX_CTRL_CHECKED))
            style = i;
    if (style < 0) {
        lv_label_set_text(act_config_error, "Choose a layout");
        return;
    }
    a.style = style;
    runtime_snapshot(state);
    if (act_edit_axis == 3 && state.online && !runtime_actuator_type(act_edit_rotary)) {
        lv_label_set_text(act_config_error, "Controller must be idle with A type known before saving.");
        return;
    }
    actuator_prefs.axes[act_edit_axis] = a;
    if (act_edit_axis == 3)
        a_rotary = act_edit_rotary;
    save();
    close_modal();
    build_page();
}
static void actuator_config(lv_event_t *e) {
    runtime_snapshot(state);
    if (state.online && (!ready() || state.sd_active)) {
        notify("Stop motion before configuring actuators.");
        return;
    }
    act_edit_axis = (uintptr_t)lv_event_get_user_data(e);
    act_edit_rotary = act_edit_axis == 3 && a_rotary;
    const auto &a = actuator_prefs.axes[act_edit_axis];
    char title[60];
    snprintf(title, sizeof title, "Configure actuator %d / %c", act_edit_axis + 1, "XYZA"[act_edit_axis]);
    lv_obj_t *c = open_modal(title, 960, 568);
    actuator_config_open = true;
    const char *names[] = {"Name", "First button", "Second button", "First position", "Second position", "Speed"};
    const char *text[] = {a.name, a.first, a.second};
    const float values[] = {a.end1, a.end2, a.feed};
    for (int i = 0; i < 6; i++) {
        int x = 20 + (i % 3) * 310, y = i < 3 ? 76 : 154;
        char label_text[80];
        snprintf(label_text, sizeof label_text, "%s%s", names[i], i < 3 ? "" :
                 i == 5 ? (inches ? " (in/min)" : " (mm/min)") : (inches ? " (in, machine)" : " (mm, machine)"));
        auto field_label = label(c, label_text, x, y, 300, 14, MUTED);
        if (i >= 3)
            act_units[i - 3] = field_label;
        auto field = act_fields[i] = lv_textarea_create(c);
        lv_obj_set_pos(field, x, y + 22);
        lv_obj_set_size(field, 300, 46);
        lv_textarea_set_one_line(field, true);
        lv_textarea_set_max_length(field, i == 0 ? 24 : i < 3 ? 16 : 12);
        lv_obj_set_style_bg_color(field, lv_color_hex(BG), 0);
        lv_obj_set_style_text_color(field, lv_color_hex(TEXT), 0);
        lv_obj_set_style_text_font(field, font(18), 0);
        lv_obj_set_style_border_width(field, 0, 0);
        char number_text[32];
        if (i >= 3) {
            lv_textarea_set_accepted_chars(field, "0123456789.-");
            snprintf(number_text, sizeof number_text, "%.3f", values[i - 3] / (act_edit_rotary ? 1 : display_scale(inches)));
        }
        lv_textarea_set_text(field, i < 3 ? text[i] : number_text);
        lv_obj_add_event_cb(field, actuator_focus, LV_EVENT_FOCUSED, (void *)(uintptr_t)i);
    }
    actuator_units();
    if (act_edit_axis == 3) {
        static const char *const types[] = {"Linear", "Rotary", ""};
        choices(c, types, act_edit_rotary, 600, 8, 340, 48, actuator_type);
    }
    button(c, "Cancel", 20, 240, 170, 48, cancelled, 0, CARD, true);
    static const char *const styles[] = {"Buttons", "Position", "Presets", "Repeat", ""};
    act_style = choices(c, styles, a.style, 220, 240, 520, 48, nullptr);
    button(c, "Save", 760, 240, 180, 48, actuator_config_save, 0, GREEN);
    act_config_error = label(c, "", 20, 288, 920, 14, RED);
    act_keyboard = lv_keyboard_create(c);
    lv_obj_set_align(act_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(act_keyboard, 20, 304);
    lv_obj_set_size(act_keyboard, 920, 244);
    lv_keyboard_set_textarea(act_keyboard, act_fields[0]);
    lv_obj_set_style_bg_color(act_keyboard, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_color(act_keyboard, lv_color_hex(CARD), LV_PART_ITEMS);
    lv_obj_set_style_text_color(act_keyboard, lv_color_hex(TEXT), LV_PART_ITEMS);
}
static bool actuator_home_confirm() {
    char axis[] = {"XYZA"[act_home_axis], 0};
    return runtime_action(Action::ActuatorHome, axis);
}
static void actuator_home_click(lv_event_t *e) {
    act_home_axis = (uintptr_t)lv_event_get_user_data(e);
    char title[64];
    snprintf(title, sizeof title, "Home %s?", actuator_prefs.axes[act_home_axis].name);
    confirm(title, "Clear the actuator's full travel before homing.", actuator_home_confirm);
}
static void actuator_move_to(int axis, float target) {
    const auto &a = actuator_prefs.axes[axis];
    result(runtime_actuator_move(axis, target, a.feed, a.end1, a.end2, axis == 3 && a_rotary));
}
static void actuator_button(lv_event_t *e) {
    unsigned id = (uintptr_t)lv_event_get_user_data(e), axis = id / 3, preset = id % 3;
    const auto &a = actuator_prefs.axes[axis];
    actuator_move_to(axis, a.end1 + (a.end2 - a.end1) * preset / 2);
}
static void actuator_slide(lv_event_t *e) {
    int axis = (uintptr_t)lv_event_get_user_data(e);
    auto slider = (lv_obj_t *)lv_event_get_target(e);
    // A release commits exactly one absolute target; dragging never queues motion.
    const auto &a = actuator_prefs.axes[axis];
    actuator_move_to(axis, a.end1 + (a.end2 - a.end1) * lv_slider_get_value(slider) / 1000);
}
static void actuator_stop_click(lv_event_t *) { result(runtime_action(Action::JogCancel)); }
static float actuator_speed_value(int axis) {
    float maximum = actuator_prefs.axes[axis].feed;
    if (state.axis_max_rate[axis] > 0)
        maximum = fminf(maximum, state.axis_max_rate[axis]);
    return act_speed_percent[axis] ? fmaxf(.1f, maximum * act_speed_percent[axis] / 100) : 0;
}
static void actuator_go(lv_event_t *e) {
    int axis = (uintptr_t)lv_event_get_user_data(e);
    const auto &a = actuator_prefs.axes[axis];
    result(runtime_actuator_cycle(axis, actuator_speed_value(axis),
                                  a.end1, a.end2, axis == 3 && a_rotary));
}
static void actuator_speed(lv_event_t *e) {
    int axis = (uintptr_t)lv_event_get_user_data(e);
    act_speed_percent[axis] = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
        return;
    runtime_snapshot(state);
    if (state.cycle_axis != axis)
        return;
    if (!act_speed_percent[axis])
        result(runtime_action(Action::JogCancel));
    else
        result(runtime_actuator_cycle_speed(actuator_speed_value(axis)));
}
static void actuator_angle(lv_event_t *) {
    const auto &a = actuator_prefs.axes[3];
    actuator_move_to(3, a.end1 + (a.end2 - a.end1) * lv_arc_get_value(act_arc) / 1000);
}
static void build_actuators() {
    act_arc = nullptr;
    memset(act_go, 0, sizeof act_go);
    memset(act_speed_text, 0, sizeof act_speed_text);
    for (int i = 0; i < 4; i++) {
        const auto &a = actuator_prefs.axes[i];
        auto card = surface(body, i * 250, 0, 234, 424);
        char title[64];
        snprintf(title, sizeof title, "%s / %c", a.name, "XYZA"[i]);
        auto heading = label(card, title, 16, 14, 202, 20, i == 3 ? 0xA95B12 : AXIS_COLORS[i]);
        lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
        lv_obj_set_height(heading, 24);
        act_home[i] = button(card, "Home", 16, 48, 202, 42, actuator_home_click, i);
        act_position[i] = label(card, "", 16, 104, 202, 28, TEXT);
        lv_obj_set_style_text_align(act_position[i], LV_TEXT_ALIGN_CENTER, 0);
        act_travel[i] = lv_bar_create(card);
        lv_obj_set_pos(act_travel[i], 16, 144);
        lv_obj_set_size(act_travel[i], 202, 6);
        lv_bar_set_range(act_travel[i], 0, 1000);
        lv_obj_set_style_bg_color(act_travel[i], lv_color_hex(0xD2DAE5), 0);
        lv_obj_set_style_bg_opa(act_travel[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(act_travel[i], lv_color_hex(i == 3 ? 0xA95B12 : AXIS_COLORS[i]), LV_PART_INDICATOR);
        if (a.style == 1 && i == 3 && a_rotary) {
            act_arc = lv_arc_create(card);
            lv_obj_set_pos(act_arc, 44, 166);
            lv_obj_set_size(act_arc, 146, 146);
            lv_arc_set_range(act_arc, 0, 1000);
            lv_arc_set_bg_angles(act_arc, 135, 45);
            lv_obj_set_style_arc_width(act_arc, 8, 0);
            lv_obj_set_style_arc_width(act_arc, 8, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(act_arc, lv_color_hex(MUTED), 0);
            lv_obj_set_style_arc_color(act_arc, lv_color_hex(MUTED), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(act_arc, lv_color_hex(0xA95B12), LV_PART_KNOB);
            lv_obj_set_style_bg_color(act_arc, lv_color_hex(MUTED), LV_PART_KNOB | LV_STATE_DISABLED);
            lv_obj_add_event_cb(act_arc, actuator_angle, LV_EVENT_RELEASED, nullptr);
        } else if (a.style == 1 || a.style == 3) {
            act_slider[i] = lv_slider_create(card);
            lv_obj_set_pos(act_slider[i], 174, 174);
            lv_obj_set_size(act_slider[i], 24, 168);
            lv_slider_set_range(act_slider[i], 0, a.style == 3 ? 100 : 1000);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(MUTED), 0);
            lv_obj_set_style_bg_opa(act_slider[i], LV_OPA_COVER, 0);
            lv_obj_set_ext_click_area(act_slider[i], 16);
            lv_obj_set_style_pad_all(act_slider[i], 12, LV_PART_KNOB);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(MUTED), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(act_slider[i], LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(DISABLED), LV_PART_INDICATOR | LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(DISABLED), LV_PART_MAIN | LV_STATE_DISABLED);
            lv_obj_set_style_bg_opa(act_slider[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(MUTED), LV_PART_KNOB | LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(act_slider[i], lv_color_hex(PRIMARY), LV_PART_KNOB);
            lv_obj_add_event_cb(act_slider[i], a.style == 3 ? actuator_speed : actuator_slide, LV_EVENT_RELEASED, (void *)(uintptr_t)i);
            if (a.style == 3) {
                lv_slider_set_value(act_slider[i], act_speed_percent[i], LV_ANIM_OFF);
                lv_obj_add_event_cb(act_slider[i], actuator_speed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
            }
        }
        if (a.style == 3) {
            act_speed_text[i] = label(card, "", 16, 174, 136, 18, MUTED);
            act_go[i] = button(card, "Go", 16, 278, 120, 64, actuator_go, i, GREEN);
        } else if (a.style == 1 && i == 3 && a_rotary) {
            act_move[i][0] = button(card, a.first, 16, 310, 96, 44, actuator_button, i * 3, 0x247C87);
            act_move[i][2] = button(card, a.second, 122, 310, 96, 44, actuator_button, i * 3 + 2, GREEN);
        } else if (a.style == 1) {
            act_move[i][2] = button(card, a.second, 16, 172, 120, 60, actuator_button, i * 3 + 2, GREEN);
            act_move[i][0] = button(card, a.first, 16, 282, 120, 60, actuator_button, i * 3, 0x247C87);
        } else {
            act_move[i][2] = button(card, a.second, 16, 168, 202, 52, actuator_button, i * 3 + 2, GREEN);
            if (a.style == 2)
                act_move[i][1] = button(card, "50%", 16, 230, 202, 52, actuator_button, i * 3 + 1, PRIMARY);
            act_move[i][0] = button(card, a.first, 16, 292, 202, 52, actuator_button, i * 3, 0x247C87);
        }
        act_stop[i] = button(card, "Stop", 16, 366, 202, 44, actuator_stop_click, 0, RED);
    }
}
static void refresh_actuators() {
    for (int i = 0; i < 4; i++) {
        const auto &a = actuator_prefs.axes[i];
        bool present = unsigned(i) < state.axis_count;
        bool homed = state.homed_axes & (1u << i);
        bool configured = a.end1 != a.end2;
        bool rotary = i == 3 && a_rotary;
        bool type_ok = i != 3 || (state.rotary_known && rotary == bool(state.rotary_axes & 8));
        bool on = ready() && state.cycle_axis < 0 && state.extended && present && homed && configured && state.machine_known &&
                  state.spindle == 0 && state.spindle_direction == 0 && type_ok;
        char text[100];
        if (!state.online || !state.status)
            snprintf(text, sizeof text, "Not connected");
        else if (!present)
            snprintf(text, sizeof text, "Axis unavailable");
        else if (!type_ok)
            snprintf(text, sizeof text, state.rotary_known ? "A type mismatch" : "Read A type");
        else if (!homed)
            snprintf(text, sizeof text, "Home required");
        else if (!state.machine_known)
            snprintf(text, sizeof text, "Position unknown");
        else
            snprintf(text, sizeof text, "%.3f %s", state.machine[i] / (rotary ? 1 : display_scale(inches)),
                     rotary ? "deg" : inches ? "in" : "mm");
        bool known = state.online && state.status && present && homed && state.machine_known && type_ok;
        lv_obj_set_style_text_font(act_position[i], font(known ? 28 : 18), 0);
        lv_label_set_text(act_position[i], text);
        int position = known && configured ? lroundf((state.machine[i] - a.end1) * 1000 / (a.end2 - a.end1)) : 0;
        position = position < 0 ? 0 : position > 1000 ? 1000 : position;
        lv_bar_set_value(act_travel[i], position, LV_ANIM_OFF);
        lv_obj_set_style_opa(act_travel[i], known && configured ? LV_OPA_COVER : LV_OPA_30, 0);
        bool can_home = state.online && state.identified && state.extended && state.status && state.cycle_axis < 0 && present && !state.busy &&
                        !state.sd_active && !state.uploading &&
                        (!strcmp(state.mode, "Idle") || !strncmp(state.mode, "Alarm", 5));
        enable(act_home[i], can_home);
        for (auto b : act_move[i])
            if (b)
                enable(b, on);
        enable(act_stop[i], state.online && state.status && !strncmp(state.mode, "Jog", 3));
        if (act_go[i]) {
            enable(act_go[i], on && act_speed_percent[i] > 0 &&
                   state.machine[i] >= fminf(a.end1, a.end2) - .01f &&
                   state.machine[i] <= fmaxf(a.end1, a.end2) + .01f);
            char speed[80];
            snprintf(speed, sizeof speed, "%d%%\n%.3f\n%s", act_speed_percent[i],
                     actuator_speed_value(i) / (rotary ? 1 : display_scale(inches)),
                     rotary ? "deg/min" : inches ? "in/min" : "mm/min");
            lv_label_set_text(act_speed_text[i], speed);
        }
        if (act_slider[i]) {
            bool running = a.style == 3 && state.cycle_axis == i && state.online && state.status &&
                           !strncmp(state.mode, "Jog", 3);
            enable(act_slider[i], on || running);
            if (!lv_obj_has_state(act_slider[i], LV_STATE_PRESSED)) {
                lv_slider_set_value(act_slider[i], a.style == 3 ? act_speed_percent[i] : position, LV_ANIM_OFF);
            }
        }
        if (i == 3 && act_arc) {
            enable(act_arc, on);
            if (!lv_obj_has_state(act_arc, LV_STATE_PRESSED))
                lv_arc_set_value(act_arc, position);
        }
    }
}
