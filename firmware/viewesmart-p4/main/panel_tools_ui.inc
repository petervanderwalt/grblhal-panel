// Included in the UI namespace so these screens share the pendant's controls.
static Macros *macros;
static constexpr uint32_t macro_colors[] = {PRIMARY, 0x247C87, 0xA95B12, GREEN};
static lv_obj_t *input_values[19], *input_connection, *accessory_feedback, *rpm_field;
static int input_display[19];
static lv_obj_t *spindle_actions[3], *coolant_actions[4], *macro_runs[MaxMacros], *macro_stop, *macro_status;
static lv_obj_t *macro_keyboard, *macro_error, *macro_colors_control;
static lv_obj_t *macro_edits[MaxMacros], *macro_add_button;
static lv_obj_t *spindle_choices[8], *spindle_list_status, *spindle_range, *spindle_select_button;
static int spindle_requested_number;
static int macro_edit_index, macro_run_index, spindle_requested_direction;
static float spindle_requested_rpm;
static const char input_codes[] = "XYZAIJKDERHSFMTLQOP";
static const char *input_names[] = {"X limit", "Y limit", "Z limit", "A limit", "Probe", "Tool setter", "Probe 2",
    "Safety door", "E-stop", "Reset", "Feed hold", "Cycle start", "Motor fault", "Motor warning",
    "Optional stop", "Block delete", "Single block", "Probe disconnected", "Selected probe"};
static void tools_text(lv_obj_t *o, const char *s) {
    if (strcmp(lv_label_get_text(o), s)) lv_label_set_text(o, s);
}
static void tools_enable(lv_obj_t *o, bool on) {
    if (o && lv_obj_has_state(o, LV_STATE_DISABLED) == on) enable(o, on);
}
static void macros_load() {
    void *memory = heap_caps_malloc(sizeof(Macros), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(memory);
    macros = new (memory) Macros;
    nvs_handle_t h;
    if (nvs_open("panelmacros", NVS_READONLY, &h) != ESP_OK) return;
    for (unsigned i = 0; i < MaxMacros; ++i) {
        auto &m = macros->entries[i];
        char key[8]; snprintf(key, sizeof key, "m%u", i);
        size_t n = sizeof m;
        if (nvs_get_blob(h, key, &m, &n) != ESP_OK || n != sizeof m ||
            !memchr(m.name, 0, sizeof m.name) || !memchr(m.code, 0, sizeof m.code) || m.color > 3) m = Macro();
    }
    nvs_close(h);
}
static bool macros_save(unsigned index) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("panelmacros", NVS_READWRITE, &h);
    if (err != ESP_OK) return false;
    char key[8]; snprintf(key, sizeof key, "m%u", index);
    if (*macros->entries[index].name) err = nvs_set_blob(h, key, &macros->entries[index], sizeof(Macro));
    else { err = nvs_erase_key(h, key); if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK; }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
static void tools_refresh() {
    bool fresh = state.online && state.status;
    if (spindles_open) {
        tools_text(spindle_list_status, state.spindles_loading ? "Loading spindles..." :
            state.spindles_result < 0 ? "Spindle discovery unavailable" : "Stop the spindle before changing selection");
        for (unsigned i = 0; i < 8; ++i) {
            bool shown = !state.spindles_loading && i < state.spindle_count;
            visibility(spindle_choices[i], shown);
            if (!shown) continue;
            const auto &s = state.spindles[i];
            char text[120]; snprintf(text, sizeof text, "%s%s%s", s.name, s.active ? " / Active" : "", s.number < 0 ? " / Not enabled" : "");
            tools_text(lv_obj_get_child(spindle_choices[i], 0), text);
            tools_enable(spindle_choices[i], ready() && state.accessories_known && !state.spindle_direction &&
                         state.spindle <= 0 && s.number >= 0 && !s.active);
        }
    }
    if (inputs_open) {
        bool reporting = !state.pins_reporting_known || state.pins_reporting;
        tools_text(input_connection, !fresh ? "Controller feedback unavailable" : !reporting ?
                   "Pin reporting is disabled in controller setting $10" : "Live inputs");
        for (unsigned i = 0; i < 19; ++i) {
            uint32_t bit = 1u << (input_codes[i]-'A');
            bool known = fresh && reporting && (state.inputs_known & bit), active = state.inputs & bit;
            if (i < 4 && i >= state.axis_count) known = false;
            if (i == 4) { known = fresh && reporting && state.probe_known; active = state.probe; }
            int value = !known ? 0 : active ? 2 : 1;
            if (input_display[i] != value) {
                input_display[i] = value;
                lv_led_set_color(input_values[i], lv_color_hex(!known ? MUTED : active ? RED : GREEN));
                lv_led_set_brightness(input_values[i], !known ? 120 : 255);
            }
        }
    }
    if (accessories_open) {
        char text[160];
        if (state.spindle_limits_known) snprintf(text, sizeof text, "Spindle RPM  /  %.0f to %.0f", state.spindle_min, state.spindle_max);
        else snprintf(text, sizeof text, "Spindle RPM  /  waiting for controller limits");
        tools_text(spindle_range, text);
        tools_enable(spindle_select_button, ready() && state.grblhal);
        if (fresh && state.accessories_known)
            snprintf(text, sizeof text, "Spindle: %s / %.0f RPM     Coolant: %s", state.spindle_direction > 0 ? "CW" :
                     state.spindle_direction < 0 ? "CCW" : "Off", state.spindle,
                     state.coolant == 3 ? "Mist + flood" : state.coolant == 2 ? "Flood" : state.coolant == 1 ? "Mist" : "Off");
        else snprintf(text, sizeof text, "Waiting for accessory feedback");
        tools_text(accessory_feedback, text);
        for (int i = 0; i < 3; ++i) tools_enable(spindle_actions[i], ready() && (!i || state.spindle_limits_known));
        for (auto b : coolant_actions) tools_enable(b, ready());
    }
    if (page == MacrosPage && !modal) {
        tools_text(macro_status, state.macro_failed ? "Macro interrupted" : state.macro_running ? "Macro running" : fresh ? state.mode : "Not connected");
        for (auto b : macro_runs) tools_enable(b, ready());
        unsigned count = 0;
        for (const auto &m : macros->entries) if (*m.name) ++count;
        tools_enable(macro_add_button, !state.macro_running && !state.macro_failed && count < MaxMacros);
        for (auto b : macro_edits) tools_enable(b, !state.macro_running && !state.macro_failed);
        visibility(macro_stop, state.macro_running || state.macro_failed);
        tools_enable(macro_stop, state.online);
    }
}
static void show_inputs(lv_event_t *) {
    auto c = open_modal("Input diagnostics", 920, 540);
    modal_close_button(c);
    input_connection = label(c, "", 24, 80, 440, 18, MUTED);
    const char *legend[] = {"Active", "Inactive", "Unknown"};
    const uint32_t colors[] = {RED, GREEN, MUTED};
    for (int i = 0; i < 3; ++i) {
        auto led = lv_led_create(c);
        lv_obj_set_pos(led, 500 + i * 132, 80); lv_obj_set_size(led, 16, 16);
        lv_led_set_color(led, lv_color_hex(colors[i]));
        lv_led_set_brightness(led, i == 2 ? 120 : 255);
        label(c, legend[i], 524 + i * 132, 80, 104, 14, MUTED);
    }
    for (unsigned i = 0; i < 19; ++i) {
        int x = 24 + (i / 7) * 294, y = 120 + (i % 7) * 56;
        label(c, input_names[i], x + 36, y, 240, 16);
        input_values[i] = lv_led_create(c);
        lv_obj_set_pos(input_values[i], x, y - 2);
        lv_obj_set_size(input_values[i], 22, 22);
        lv_obj_remove_flag(input_values[i], LV_OBJ_FLAG_CLICKABLE);
        input_display[i] = -1;
    }
    inputs_open = true;
    runtime_snapshot(state); tools_refresh();
}
static void show_accessories(lv_event_t * = nullptr);
static bool activate_spindle() {
    bool ok = runtime_action(Action::SpindleSelect, nullptr, spindle_requested_number);
    if (ok) show_accessories();
    return ok;
}
static void select_spindle(lv_event_t *e) {
    unsigned i = (uintptr_t)lv_event_get_user_data(e);
    runtime_snapshot(state);
    if (i >= state.spindle_count) return;
    spindle_requested_number = state.spindles[i].number;
    char text[120]; snprintf(text, sizeof text, "Use %s as the active spindle?", state.spindles[i].name);
    confirm("Activate spindle?", text, activate_spindle, AMBER, "Activate");
}
static void show_spindles(lv_event_t *) {
    bool ok = runtime_action(Action::SpindleList);
    if (!ok) { result(false); return; }
    auto c = open_modal("Select spindle", 920, 550);
    modal_close_button(c);
    spindle_list_status = label(c, "", 24, 80, 872, 18, MUTED);
    for (unsigned i = 0; i < 8; ++i)
        spindle_choices[i] = button(c, "", 24 + (i % 2) * 440, 124 + (i / 2) * 96, 424, 78, select_spindle, i);
    spindles_open = true;
    runtime_snapshot(state); tools_refresh();
}
static bool start_spindle() {
    bool ok = runtime_action(Action::Spindle, nullptr, spindle_requested_direction, spindle_requested_rpm);
    if (ok) show_accessories();
    return ok;
}
static void spindle_click(lv_event_t *e) {
    spindle_requested_direction = (int)(intptr_t)lv_event_get_user_data(e);
    char *end;
    const char *s = lv_textarea_get_text(rpm_field);
    spindle_requested_rpm = strtof(s, &end);
    runtime_snapshot(state);
    if (spindle_requested_direction && (!*s || *end || !isfinite(spindle_requested_rpm) ||
        spindle_requested_rpm <= 0 || spindle_requested_rpm < state.spindle_min || spindle_requested_rpm > state.spindle_max)) {
        tools_text(accessory_feedback, "Enter an RPM within the controller range"); return;
    }
    if (!spindle_requested_direction) { result(runtime_action(Action::Spindle)); return; }
    char question[96]; snprintf(question, sizeof question, "Start %s at %.0f RPM?", spindle_requested_direction > 0 ? "CW" : "CCW", spindle_requested_rpm);
    confirm("Start spindle?", question, start_spindle, AMBER, "Start");
}
static void coolant_click(lv_event_t *e) {
    result(runtime_action(Action::Coolant, nullptr, (uintptr_t)lv_event_get_user_data(e)));
}
static void show_accessories(lv_event_t *) {
    runtime_snapshot(state);
    auto c = open_modal("Spindle / coolant", 920, 550);
    modal_close_button(c);
    spindle_select_button = button(c, "Select spindle", 610, 10, 220, 44, show_spindles);
    char range[100];
    if (state.spindle_limits_known) snprintf(range, sizeof range, "Spindle RPM  /  %.0f to %.0f", state.spindle_min, state.spindle_max);
    else snprintf(range, sizeof range, "Spindle RPM  /  waiting for controller limits");
    spindle_range = label(c, range, 24, 80, 850, 18, MUTED);
    rpm_field = lv_textarea_create(c);
    lv_obj_set_pos(rpm_field, 24, 112); lv_obj_set_size(rpm_field, 228, 58);
    lv_textarea_set_one_line(rpm_field, true); lv_textarea_set_max_length(rpm_field, 9);
    lv_textarea_set_accepted_chars(rpm_field, "0123456789");
    char rpm[24]; snprintf(rpm, sizeof rpm, "%.0f", spindle_requested_rpm > 0 ? spindle_requested_rpm : state.spindle_min);
    lv_textarea_set_text(rpm_field, rpm);
    spindle_actions[1] = button(c, "CW", 272, 112, 196, 58, spindle_click, 1, PRIMARY);
    spindle_actions[2] = button(c, "CCW", 480, 112, 196, 58, spindle_click, (uintptr_t)-1, PRIMARY);
    spindle_actions[0] = button(c, "Off", 688, 112, 208, 58, spindle_click);
    label(c, "Coolant", 24, 192, 850, 18, MUTED);
    const char *names[] = {"Off", "Mist", "Flood", "Mist + flood"};
    for (int i = 0; i < 4; ++i) coolant_actions[i] = button(c, names[i], 24 + i * 220, 220, 208, 58, coolant_click, i);
    accessory_feedback = label(c, "", 24, 302, 872, 18);
    auto keyboard = lv_keyboard_create(c);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 24, 342); lv_obj_set_size(keyboard, 872, 188);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER); lv_keyboard_set_textarea(keyboard, rpm_field);
    accessories_open = true; tools_refresh();
}
static void macro_edit(lv_event_t *);
static void show_macro_editor();
static bool macro_run_confirmed() { return runtime_action(Action::Macro, macros->entries[macro_run_index].code); }
static void macro_run_click(lv_event_t *e) {
    macro_run_index = (uintptr_t)lv_event_get_user_data(e);
    confirm("Run macro?", macros->entries[macro_run_index].name, macro_run_confirmed, AMBER, "Run");
}
static bool macro_delete_confirmed() {
    Macro old = macros->entries[macro_edit_index];
    macros->entries[macro_edit_index] = Macro();
    if (!macros_save(macro_edit_index)) { macros->entries[macro_edit_index] = old; return false; }
    build_page(); return true;
}
static void macro_delete(lv_event_t *e) {
    macro_edit_index = (uintptr_t)lv_event_get_user_data(e);
    confirm("Delete macro?", macros->entries[macro_edit_index].name, macro_delete_confirmed, RED, "Delete", RED);
}
static void macro_stop_click(lv_event_t *) {
    confirm("Stop macro?", "The controller will reset. The macro cannot resume.", stop, RED, "Stop", RED);
}
static void macro_add(lv_event_t *) {
    for (unsigned i = 0; i < MaxMacros; ++i) if (!*macros->entries[i].name) {
        macro_edit_index = i;
        lv_async_call([](void *) { show_macro_editor(); }, nullptr);
        return;
    }
    notify("All 20 macro positions are in use.");
}
static void build_macros() {
    memset(macro_runs, 0, sizeof macro_runs);
    memset(macro_edits, 0, sizeof macro_edits);
    label(body, "Macros", 20, 18, 300, 22);
    macro_status = label(body, "", 200, 22, 330, 18, MUTED);
    macro_add_button = button(body, LV_SYMBOL_PLUS "  Add macro", 780, 10, 192, 48, macro_add, 0, PRIMARY);
    macro_stop = button(body, "Stop macro", 562, 10, 202, 48, macro_stop_click, 0, RED);
    auto list = box(body, 12, 70, 968, 344, CARD);
    lv_obj_set_style_border_width(list, 0, 0); lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (unsigned i = 0; i < MaxMacros; ++i) {
        const auto &m = macros->entries[i];
        int x = (i % 4) * 244, y = (i / 4) * 68;
        if (!*m.name) continue;
        macro_runs[i] = button(list, m.name, x, y, 170, 58, macro_run_click, i, macro_colors[m.color]);
        lv_label_set_long_mode(lv_obj_get_child(macro_runs[i], 0), LV_LABEL_LONG_CLIP);
        macro_edits[i] = button(list, LV_SYMBOL_EDIT, x + 176, y, 56, 58, macro_edit, i);
    }
    tools_refresh();
}
static void macro_focus(lv_event_t *e) { lv_keyboard_set_textarea(macro_keyboard, (lv_obj_t *)lv_event_get_target(e)); }
static void macro_changed(lv_event_t *) { if (macro_editor_open) tools_text(macro_error, ""); }
static void macro_saved(lv_event_t *) {
    const char *name = lv_textarea_get_text(macro_fields[0]), *code = lv_textarea_get_text(macro_fields[1]);
    std::vector<std::string> lines;
    bool named = false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) if (*p > 32) named = true;
    if (!named || strlen(name) > 32 || !macro_lines(code, lines)) {
        tools_text(macro_error, "Use a name and up to 32 G-code lines. No system commands or ! ~ ?."); return;
    }
    Macro old = macros->entries[macro_edit_index], &m = macros->entries[macro_edit_index];
    snprintf(m.name, sizeof m.name, "%s", name); snprintf(m.code, sizeof m.code, "%s", code);
    // The checked choice exists before the user clicks a matrix button.
    m.color = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (lv_buttonmatrix_has_button_ctrl(macro_colors_control, i, LV_BUTTONMATRIX_CTRL_CHECKED)) m.color = i;
    if (!macros_save(macro_edit_index)) { m = old; tools_text(macro_error, "Could not save macro"); return; }
    lv_async_call([](void *) { close_modal(); build_page(); }, nullptr);
}
static void macro_edit(lv_event_t *e) {
    macro_edit_index = (uintptr_t)lv_event_get_user_data(e);
    show_macro_editor();
}
static void show_macro_editor() {
    const auto &m = macros->entries[macro_edit_index];
    auto c = open_modal(*m.name ? "Edit macro" : "Add macro", 1000, 568);
    modal_close_button(c, 1000);
    label(c, "Name", 24, 76, 550, 16, MUTED);
    label(c, "Colour", 640, 76, 330, 16, MUTED);
    for (int i = 0; i < 2; ++i) {
        macro_fields[i] = lv_textarea_create(c);
        lv_obj_set_pos(macro_fields[i], 24, i ? 190 : 100);
        lv_obj_set_size(macro_fields[i], i ? 952 : 592, i ? 104 : 54);
        lv_textarea_set_max_length(macro_fields[i], i ? MaxMacroCode : 32);
        lv_textarea_set_one_line(macro_fields[i], !i);
        lv_textarea_set_text(macro_fields[i], i ? m.code : m.name);
        lv_obj_add_event_cb(macro_fields[i], macro_focus, LV_EVENT_FOCUSED, nullptr);
    }
    static const char *const colors[] = {"Blue", "Teal", "Amber", "Green", ""};
    macro_colors_control = choices(c, colors, m.color, 640, 100, 336, 54, nullptr);
    label(c, "G-code", 24, 166, 650, 16, MUTED);
    macro_keyboard = lv_keyboard_create(c);
    lv_obj_set_align(macro_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(macro_keyboard, 24, 306); lv_obj_set_size(macro_keyboard, 952, 178);
    lv_keyboard_set_textarea(macro_keyboard, macro_fields[0]);
    macro_error = label(c, "", 196, 500, 560, 14, RED);
    for (auto field : macro_fields) lv_obj_add_event_cb(field, macro_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    button(c, "Save", 780, 500, 196, 52, macro_saved, 0, GREEN);
    if (*m.name) button(c, "Delete", 24, 500, 156, 52, macro_delete, macro_edit_index, RED);
    macro_editor_open = true;
}
