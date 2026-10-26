// Included in panel_ui.cpp to share its modal and control styles.
static SettingsCatalog machine_catalog;
static MachineSetting machine_setting;
static bool machine_browser, machine_editor, machine_search, machine_pending, machine_dirty;
static lv_obj_t *machine_groups, *machine_rows, *machine_caption, *machine_status, *machine_spinner;
static lv_obj_t *machine_field, *machine_save, *machine_error, *machine_choice, *machine_refresh_btn;
static lv_obj_t *machine_previous, *machine_next, *machine_page_text;
static std::vector<lv_obj_t *> machine_row_buttons, machine_checks;
static std::vector<std::string> machine_options;
static int machine_group = -1, machine_page = 0, machine_edit_id;
static int32_t machine_scroll = 0;
static uint32_t machine_seen = UINT32_MAX, machine_mask;
static std::string machine_query;
static void machine_browser_open(bool reload);
static void machine_render();

static void machine_closed() {
    machine_browser = machine_editor = machine_search = machine_pending = false;
    machine_groups = machine_rows = machine_caption = machine_status = machine_spinner = nullptr;
    machine_field = machine_save = machine_error = machine_choice = machine_refresh_btn = nullptr;
    machine_previous = machine_next = machine_page_text = nullptr;
    machine_row_buttons.clear(); machine_checks.clear();
}
static std::string machine_lower(std::string s) {
    for (char &c : s) c = char(tolower((unsigned char)c));
    return s;
}
static bool machine_member(const MachineSetting &s) {
    if (machine_group == -2) {
        for (const auto &g : machine_catalog.groups) if (g.id == s.group) return false;
        return true;
    }
    return machine_catalog.in_group(s.group, machine_group);
}
static std::vector<int> machine_filtered() {
    std::vector<int> out;
    auto query = machine_lower(machine_query);
    for (const auto &s : machine_catalog.entries) {
        if (!machine_member(s)) continue;
        auto text = machine_lower("$" + std::to_string(s.id) + " " + s.name + " " + s.unit + " " + s.description);
        if (query.empty() || text.find(query) != std::string::npos) out.push_back(s.id);
    }
    std::sort(out.begin(), out.end());
    return out;
}
static void machine_render_later() {
    lv_async_call([](void *) { if (machine_browser) machine_render(); }, nullptr);
}
static void machine_group_click(lv_event_t *e) {
    machine_group = int((intptr_t)lv_event_get_user_data(e));
    machine_page = 0; machine_scroll = 0; machine_render_later();
}
static void machine_page_click(lv_event_t *e) {
    machine_page += int((intptr_t)lv_event_get_user_data(e));
    machine_scroll = 0; machine_render_later();
}
static void machine_back(lv_event_t *) {
    lv_async_call([](void *) { machine_browser_open(false); }, nullptr);
}
static void machine_field_changed(lv_event_t *) { machine_dirty = true; }
static void machine_search_save(lv_event_t *) {
    machine_query = lv_textarea_get_text(machine_field);
    machine_group = -1; machine_page = 0; machine_scroll = 0;
    machine_back(nullptr);
}
static void machine_search_open(lv_event_t *) {
    auto c = open_modal("Find a machine setting", 1000, 568);
    machine_search = true;
    modal_close_button(c, 1000, machine_back);
    machine_field = lv_textarea_create(c);
    lv_obj_set_pos(machine_field, 20, 92); lv_obj_set_size(machine_field, 740, 54);
    lv_textarea_set_one_line(machine_field, true); lv_textarea_set_max_length(machine_field, 64);
    lv_textarea_set_placeholder_text(machine_field, "Name, description or $ number");
    lv_textarea_set_text(machine_field, machine_query.c_str());
    lv_obj_set_style_text_font(machine_field, font(22), 0);
    button(c, "Find", 780, 92, 200, 54, machine_search_save, 0, PRIMARY);
    auto keyboard = lv_keyboard_create(c);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 20, 184); lv_obj_set_size(keyboard, 960, 364);
    lv_keyboard_set_textarea(keyboard, machine_field);
    finish_modal(c);
}
static void machine_mask_changed(lv_event_t *e) {
    unsigned bit = (uintptr_t)lv_event_get_user_data(e);
    if (lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED)) machine_mask |= uint32_t(1) << bit;
    else machine_mask &= ~(uint32_t(1) << bit);
    if (machine_setting.type == 2) for (size_t i = 1; i < machine_checks.size(); i++)
        if (machine_checks[i]) enable(machine_checks[i], (machine_mask & 1) != 0);
}
static void machine_save_click(lv_event_t *) {
    std::string value;
    if (machine_setting.type == 0) {
        for (int i = 0; i < 2; i++) if (lv_buttonmatrix_has_button_ctrl(machine_choice, i, LV_BUTTONMATRIX_CTRL_CHECKED)) value = std::to_string(i);
    } else if (machine_setting.type == 3 && machine_choice) value = std::to_string(lv_dropdown_get_selected(machine_choice));
    else if (!machine_checks.empty()) value = std::to_string(machine_mask);
    else if (machine_field) {
        if (machine_setting.type == 8 && !machine_dirty) { machine_back(nullptr); return; }
        value = lv_textarea_get_text(machine_field);
    }
    std::string error;
    if (!SettingsCatalog::validate(machine_setting, value, error)) { lv_label_set_text(machine_error, error.c_str()); return; }
    if (!runtime_setting_write(machine_setting.id, value.c_str())) {
        runtime_snapshot(state);
        lv_label_set_text(machine_error, state.settings_message[0] ? state.settings_message : "Controller must be connected and idle");
        return;
    }
    machine_pending = true;
    lv_label_set_text(machine_error, "Saving and checking readback...");
    enable(machine_save, false);
}
static void machine_editor_open(void *) {
    const auto *found = machine_catalog.find(machine_edit_id);
    if (!found) return;
    machine_setting = *found;
    char title[320]; snprintf(title, sizeof title, "$%d  %s", machine_setting.id, machine_setting.name.c_str());
    auto c = open_modal(title, 1000, 568);
    machine_editor = true; machine_dirty = false;
    modal_close_button(c, 1000, machine_back);
    auto description = box(c, 20, 78, 960, 88, CARD);
    lv_obj_set_style_border_width(description, 0, 0);
    lv_obj_add_flag(description, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(description, LV_DIR_VER);
    label(description, machine_setting.description.empty() ? "No description supplied by the controller." : machine_setting.description.c_str(), 0, 0, 930, 18, MUTED);
    std::string detail = machine_setting.unit;
    if (!machine_setting.minimum.empty()) detail += "   Min: " + machine_setting.minimum;
    if (!machine_setting.maximum.empty()) detail += "   Max: " + machine_setting.maximum;
    if (machine_setting.reboot) detail += "   Controller restart required";
    label(c, detail.c_str(), 20, 174, 960, 14, machine_setting.reboot ? 0xA95B12 : MUTED);
    machine_options = SettingsCatalog::options(machine_setting, state.axis_count);
    bool mask = (machine_setting.type == 1 || machine_setting.type == 2 || machine_setting.type == 4) && !machine_options.empty();
    bool list = machine_setting.type == 0 || mask || (machine_setting.type == 3 && !machine_options.empty());
    if (!machine_setting.metadata || !machine_setting.writable || !machine_setting.has_value) {
        label(c, "Controller metadata and a current value are required for editing.", 20, 220, 960, 20, MUTED);
        button(c, "OK", 20, 490, 200, 52, machine_back, 0, GREEN);
        return;
    }
    machine_error = label(c, "", 20, list ? 462 : 264, 960, 14, RED);
    if (machine_setting.type == 0) {
        static const char *const toggle[] = {"Off", "On", ""};
        machine_choice = choices(c, toggle, atoi(machine_setting.value.c_str()) ? 1 : 0, 20, 216, 460, 64, nullptr);
    } else if (mask) {
        machine_mask = uint32_t(strtoul(machine_setting.value.c_str(), nullptr, 10));
        auto area = box(c, 20, 206, 960, 240, CARD);
        lv_obj_set_style_border_width(area, 0, 0);
        lv_obj_add_flag(area, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(area, LV_DIR_VER);
        machine_checks.resize(std::min(machine_options.size(), size_t(32)), nullptr);
        for (size_t i = 0; i < machine_checks.size(); i++) {
            if (machine_options[i].empty() || machine_options[i] == "N/A") continue;
            auto check = machine_checks[i] = lv_checkbox_create(area);
            lv_checkbox_set_text(check, machine_options[i].c_str());
            lv_obj_set_pos(check, 12 + (i % 2) * 470, 10 + (i / 2) * 54);
            lv_obj_set_size(check, 440, 48); lv_obj_set_style_text_font(check, font(18), 0);
            if (machine_mask & (uint32_t(1) << i)) lv_obj_add_state(check, LV_STATE_CHECKED);
            if (machine_setting.type == 2 && i > 0) enable(check, (machine_mask & 1) != 0);
            lv_obj_add_event_cb(check, machine_mask_changed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
        }
    } else if (machine_setting.type == 3 && !machine_options.empty()) {
        std::string options;
        for (const auto &option : machine_options) { if (!options.empty()) options += '\n'; options += option.empty() ? "N/A" : option; }
        machine_choice = dropdown(c, options.c_str(), atoi(machine_setting.value.c_str()), 20, 220, 660, nullptr);
    } else {
        machine_field = lv_textarea_create(c);
        lv_obj_set_pos(machine_field, 20, 206); lv_obj_set_size(machine_field, 520, 52);
        lv_textarea_set_one_line(machine_field, true); lv_textarea_set_max_length(machine_field, MaxSettingValue);
        lv_obj_set_style_text_font(machine_field, font(22), 0);
        lv_obj_set_style_bg_color(machine_field, lv_color_hex(BG), 0);
        lv_obj_set_style_text_color(machine_field, lv_color_hex(TEXT), 0);
        if (machine_setting.type <= 6) lv_textarea_set_accepted_chars(machine_field, machine_setting.type == 6 ? "0123456789.-+eE" : "0123456789");
        if (machine_setting.type == 9) lv_textarea_set_accepted_chars(machine_field, "0123456789.");
        if (machine_setting.type == 8) {
            lv_textarea_set_password_mode(machine_field, true);
            lv_textarea_set_placeholder_text(machine_field, "Enter replacement password");
        } else lv_textarea_set_text(machine_field, machine_setting.value.c_str());
        lv_obj_add_event_cb(machine_field, machine_field_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        auto keyboard = lv_keyboard_create(c);
        lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(keyboard, 20, 288); lv_obj_set_size(keyboard, 960, 260);
        lv_keyboard_set_textarea(keyboard, machine_field);
        lv_keyboard_set_mode(keyboard, machine_setting.type <= 6 || machine_setting.type == 9 ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(BG), 0);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(CARD), LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(TEXT), LV_PART_ITEMS);
    }
    button(c, "Cancel", list ? 20 : 560, list ? 490 : 206, 200, 52, machine_back, 0, CARD, true);
    machine_save = button(c, "Save", 780, list ? 490 : 206, 200, 52, machine_save_click, 0, GREEN);
    finish_modal(c);
}
static void machine_edit_click(lv_event_t *e) {
    machine_edit_id = (uintptr_t)lv_event_get_user_data(e);
    machine_scroll = lv_obj_get_scroll_y(machine_rows);
    lv_async_call(machine_editor_open, nullptr);
}
static std::string machine_display(const MachineSetting &s) {
    if (!s.metadata || !s.has_value) return "Unavailable";
    if (s.type == 8) return "********";
    if (s.type == 0) return atoi(s.value.c_str()) ? "On" : "Off";
    auto options = SettingsCatalog::options(s, state.axis_count);
    if (s.type == 3) {
        unsigned n = strtoul(s.value.c_str(), nullptr, 10);
        if (n < options.size()) return options[n];
    }
    if ((s.type == 1 || s.type == 2 || s.type == 4) && !options.empty()) {
        uint32_t mask = strtoul(s.value.c_str(), nullptr, 10); std::string result; unsigned count = 0;
        for (size_t i = 0; i < options.size() && i < 32; i++) if ((mask & (uint32_t(1) << i)) && options[i] != "N/A") {
            if (!result.empty()) result += ", ";
            result += options[i];
            count++;
        }
        if (result.size() > 22) return std::to_string(count) + " selected";
        if (!result.empty()) return result;
        return "None";
    }
    return s.value;
}
static void machine_render() {
    if (!machine_browser) return;
    int32_t scroll = lv_obj_get_scroll_y(machine_groups);
    lv_obj_clean(machine_groups); lv_obj_clean(machine_rows); machine_row_buttons.clear();
    int y = 4;
    auto group_button = [&](int id, const std::string &name) {
        auto b = button(machine_groups, name.c_str(), 4, y, 224, 44, machine_group_click, (uintptr_t)(intptr_t)id, CARD);
        lv_obj_set_style_bg_color(b, lv_color_hex(CARD), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(id == machine_group ? PRIMARY : TEXT), 0);
        lv_obj_set_style_border_width(b, id == machine_group ? 2 : 0, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(PRIMARY), 0);
        lv_obj_set_style_text_align(lv_obj_get_child(b, 0), LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(lv_obj_get_child(b, 0), LV_LABEL_LONG_CLIP);
        lv_obj_set_height(lv_obj_get_child(b, 0), 22);
        if (id == machine_group) lv_obj_add_state(b,LV_STATE_CHECKED);
        y += 48;
    };
    group_button(-1, "All settings");
    auto groups = machine_catalog.groups;
    std::sort(groups.begin(), groups.end(), [](const SettingGroup &a, const SettingGroup &b) { return a.id < b.id; });
    for (const auto &g : groups) {
        unsigned count = 0; for (const auto &s : machine_catalog.entries) if (machine_catalog.in_group(s.group, g.id)) count++;
        if (!count) continue;
        std::string name = g.parent && g.parent != g.id ? "  " : "";
        group_button(g.id, name + g.name + " (" + std::to_string(count) + ")");
    }
    bool other = false;
    for (const auto &s : machine_catalog.entries) { bool known = false; for (const auto &g : groups) if (g.id == s.group) known = true; if (!known) other = true; }
    if (other) group_button(-2, "Other");
    lv_obj_scroll_to_y(machine_groups, scroll, LV_ANIM_OFF);
    auto ids = machine_filtered();
    int pages = std::max(1, int((ids.size() + 19) / 20));
    machine_page = std::max(0, std::min(machine_page, pages - 1));
    int start = machine_page * 20, end = std::min(start + 20, int(ids.size()));
    for (int i = start; i < end; i++) {
        const auto &s = *machine_catalog.find(ids[i]);
        auto row = surface(machine_rows, 0, (i - start) * 74, 680, 68);
        auto title = label(row, s.name.c_str(), 12, 8, 444, 18);
        lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP); lv_obj_set_height(title, 24);
        std::string detail = "$" + std::to_string(s.id) + (s.unit.empty() ? "" : "  /  " + s.unit);
        label(row, detail.c_str(), 12, 38, 444, 14, MUTED);
        auto value = machine_display(s);
        if (value.size() > 22) value = value.substr(0, 19) + "...";
        auto b = button(row, value.c_str(), 470, 8, 194, 52, machine_edit_click, s.id);
        lv_label_set_long_mode(lv_obj_get_child(b, 0), LV_LABEL_LONG_CLIP); lv_obj_set_height(lv_obj_get_child(b, 0), 24);
        machine_row_buttons.push_back(b);
    }
    if (ids.empty()) label(machine_rows, state.settings_loading ? "Reading settings..." : "No matching settings", 16, 16, 640, 20, MUTED);
    lv_obj_scroll_to_y(machine_rows, machine_scroll, LV_ANIM_OFF);
    char page[80]; snprintf(page, sizeof page, "%d-%d / %u", ids.empty() ? 0 : start + 1, end, unsigned(ids.size()));
    lv_label_set_text(machine_page_text, page);
    enable(machine_previous, machine_page > 0); enable(machine_next, machine_page + 1 < pages);
    std::string caption = machine_query.empty() ? "All settings" : "Find: " + machine_query;
    for (const auto &g : groups) if (g.id == machine_group) caption = g.name;
    if (machine_group == -2) caption = "Other";
    lv_label_set_text(machine_caption, caption.c_str());
    form_control_style(machine_groups); form_control_style(machine_rows);
}
static void machine_reload(lv_event_t *) {
    if (runtime_settings_load()) { machine_catalog.clear(); machine_seen = UINT32_MAX; machine_render_later(); }
}
static void machine_browser_open(bool reload) {
    runtime_snapshot(state);
    auto c = open_modal("Machine settings", 1000, 568);
    machine_browser = true;
    modal_close_button(c, 1000);
    machine_refresh_btn = button(c, "Refresh", 20, 80, 140, 48, machine_reload);
    button(c, "Find", 176, 80, 140, 48, machine_search_open);
    machine_caption = label(c, "", 340, 78, 540, 20);
    lv_label_set_long_mode(machine_caption, LV_LABEL_LONG_CLIP); lv_obj_set_height(machine_caption, 26);
    machine_status = label(c, "", 340, 108, 570, 14, MUTED);
    machine_spinner = lv_spinner_create(c); lv_obj_set_pos(machine_spinner, 928, 86); lv_obj_set_size(machine_spinner, 32, 32);
    machine_groups = box(c, 20, 144, 240, 340, CARD);
    machine_rows = box(c, 280, 144, 700, 340, CARD);
    for (auto p : {machine_groups, machine_rows}) {
        lv_obj_set_style_border_width(p, 0, 0); lv_obj_set_style_radius(p, 0, 0);
        lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(p, LV_DIR_VER);
    }
    machine_previous = button(c, "Previous", 280, 500, 140, 48, machine_page_click, (uintptr_t)(intptr_t)-1);
    machine_next = button(c, "Next", 840, 500, 140, 48, machine_page_click, 1);
    machine_page_text = label(c, "", 460, 514, 340, 18, MUTED);
    lv_obj_set_style_text_align(machine_page_text, LV_TEXT_ALIGN_CENTER, 0);
    if (reload) { machine_group = -1; machine_page = 0; machine_scroll = 0; machine_query.clear(); runtime_settings_load(); runtime_snapshot(state); }
    if (!state.settings_loading && !state.settings_saving) runtime_settings_snapshot(machine_catalog);
    else machine_catalog.clear();
    machine_seen = state.settings_revision;
    machine_render();
    finish_modal(c);
}
static void show_machine_settings(lv_event_t *) { machine_browser_open(true); }
static void machine_refresh() {
    auto update_enabled = [](lv_obj_t *object, bool on) {
        if (object && lv_obj_has_state(object, LV_STATE_DISABLED) == on) enable(object, on);
    };
    if (machine_editor) {
        if (machine_pending && !state.settings_saving) {
            machine_pending = false;
            if (state.settings_result == 1) {
                if (machine_setting.id == 376 && state.rotary_known) { a_rotary = state.rotary_axes & 8; save(); }
                machine_browser_open(false); return;
            }
            lv_label_set_text(machine_error, state.settings_message);
        }
        update_enabled(machine_save, ready() && state.settings_ready && !machine_pending);
    }
    if (!machine_browser) return;
    bool reading = state.settings_loading || state.settings_saving;
    if (!reading && machine_seen != state.settings_revision) {
        runtime_settings_snapshot(machine_catalog); machine_seen = state.settings_revision; machine_render();
    }
    char status[192];
    if (!state.online) snprintf(status, sizeof status, "Connect a grblHAL controller");
    else if (reading) snprintf(status, sizeof status, "Reading %d settings...", state.settings_count);
    else snprintf(status, sizeof status, "%s  /  %d settings", state.settings_message[0] ? state.settings_message : "Refresh to load", state.settings_count);
    if (strcmp(lv_label_get_text(machine_status), status)) lv_label_set_text(machine_status, status);
    visibility(machine_spinner, reading);
    update_enabled(machine_refresh_btn, ready() && state.grblhal && !reading);
    for (auto b : machine_row_buttons) update_enabled(b, !reading);
}
