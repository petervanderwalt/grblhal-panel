// Card treatment for Jog and Files; existing actions remain intact.
// USER_1 confines visual overrides to styled pages; rebuild restores geometry.
constexpr uint32_t CONCEPT_BG = 0x34373C, CONCEPT_CARD = 0x080B0D,
                   CONCEPT_PAD = 0x22272B, CONCEPT_INK = 0xF2F5F4,
                   CONCEPT_MUTED = 0x94A0A5, CONCEPT_LIME = 0xD9F95C;
constexpr uint32_t CONCEPT_AXES[] = {0xFF8379, 0x83E6B0, 0x78DEEF};
static bool concept_active() { return page == JogPage && !actuator_mode; }
static bool concept_header_active() { return tab_for_page(page) >= 0 || page == JobPage || page == CompletePage; }
struct ConceptGeometry { lv_obj_t *obj; int x, y, w, h; };
static ConceptGeometry concept_geometry[48];
static unsigned concept_geometry_count;
static lv_obj_t *concept_action_icons[6];
static void concept_place(lv_obj_t *o, int x, int y, int w, int h, bool persistent = true) {
    if (persistent) {
        bool found = false;
        for (unsigned i = 0; i < concept_geometry_count; ++i) found |= concept_geometry[i].obj == o;
        if (!found && concept_geometry_count < 48)
            concept_geometry[concept_geometry_count++] = {o, lv_obj_get_x(o), lv_obj_get_y(o), lv_obj_get_width(o), lv_obj_get_height(o)};
    }
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
}
static void concept_clear(lv_obj_t *o) {
    lv_obj_remove_state(o, LV_STATE_USER_1);
    for (unsigned i = 0; i < lv_obj_get_child_count(o); ++i) concept_clear(lv_obj_get_child(o, i));
}
static void concept_restore() {
    concept_clear(root);
    for (auto icon : concept_action_icons) if (icon) visibility(icon, false);
    for (unsigned i = 0; i < concept_geometry_count; ++i) {
        const auto &g = concept_geometry[i];
        lv_obj_set_pos(g.obj, g.x, g.y); lv_obj_set_size(g.obj, g.w, g.h);
    }
    visibility(brand_label, true);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lv_obj_get_child(accessories_btn, 0), "Spindle /\ncoolant");
    lv_label_set_text(work_heading, "WORK");
    for (int i = 0; i < 3; ++i) {
        char text[12]; snprintf(text, sizeof text, "Home %c", "XYZ"[i]);
        lv_label_set_text(lv_obj_get_child(axis_home[i], 0), text);
    }
    lv_label_set_text(lv_obj_get_child(home_btn, 0), "Home all");
    lv_label_set_text(lv_obj_get_child(goto_buttons[0], 0), "Go to XY\nzero");
    lv_label_set_text(lv_obj_get_child(goto_buttons[1], 0), "Go to Z\nzero");
    lv_label_set_text(lv_obj_get_child(zero_all_btn, 0), "Set zero\nall");
}
static void concept_text(lv_obj_t *o, uint32_t ink, const lv_font_t *type = nullptr) {
    lv_obj_add_state(o, LV_STATE_USER_1);
    lv_obj_set_style_text_color(o, lv_color_hex(ink), LV_STATE_USER_1);
    if (type) lv_obj_set_style_text_font(o, type, LV_STATE_USER_1);
}
static void concept_skin(lv_obj_t *o, uint32_t fill, int radius = 16) {
    lv_obj_add_state(o, LV_STATE_USER_1);
    for (lv_state_t state : {(lv_state_t)LV_STATE_USER_1, (lv_state_t)(LV_STATE_USER_1 | LV_STATE_DISABLED)}) {
        lv_obj_set_style_bg_color(o, lv_color_hex(fill), state);
        lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, state);
        lv_obj_set_style_border_width(o, 0, state);
        lv_obj_set_style_shadow_width(o, 0, state);
        lv_obj_set_style_radius(o, radius, state);
    }
}
static void concept_button(lv_obj_t *o, uint32_t fill = CONCEPT_PAD, uint32_t ink = CONCEPT_INK) {
    concept_skin(o, fill);
    if (lv_obj_get_child_count(o)) concept_text(lv_obj_get_child(o, 0), ink, font(16));
    lv_obj_set_style_bg_color(o, lv_color_hex(0x3B4449), LV_STATE_USER_1 | LV_STATE_PRESSED);
}
static void concept_action_icon(lv_obj_t *button, unsigned index, const char *glyph) {
    auto &icon = concept_action_icons[index];
    if (!icon) {
        icon = lv_label_create(button);
        lv_label_set_text(icon, glyph);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    bool stacked = index < 4;
    lv_obj_set_pos(icon, stacked ? 40 : index == 4 ? 28 : 47, stacked ? 7 : 18);
    lv_obj_set_size(icon, 24, 26);
    visibility(icon, true);
    concept_text(icon, CONCEPT_INK, &panel_action_icons_22);
    auto caption = lv_obj_get_child(button, 0);
    lv_obj_set_style_align(caption, LV_ALIGN_TOP_LEFT, LV_STATE_USER_1);
    lv_obj_set_style_x(caption, stacked ? 4 : index == 4 ? 58 : 77, LV_STATE_USER_1);
    lv_obj_set_style_y(caption, stacked ? 36 : 20, LV_STATE_USER_1);
    lv_obj_set_style_width(caption, stacked ? 96 : index == 4 ? 64 : 72, LV_STATE_USER_1);
    lv_obj_set_style_height(caption, 22, LV_STATE_USER_1);
    lv_obj_set_style_text_align(caption, stacked ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, LV_STATE_USER_1);
}
// Library headings sit above a normal rounded card, leaving room for the status header.
static void concept_library_card() {
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_STATE_USER_1);
    auto card = box(body, 0, 60, 992, 450, CONCEPT_CARD);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_move_background(card);
}
static void concept_dock() {
    lv_obj_set_pos(tabview, 16, 540); lv_obj_set_size(tabview, 992, 96);
    lv_tabview_set_tab_bar_size(tabview, 96);
    auto bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CONCEPT_CARD), 0);
    lv_obj_set_style_radius(bar, 26, 0);
    lv_obj_set_style_pad_all(bar, 5, 0); lv_obj_set_style_pad_column(bar, 6, 0);
    // Extra bottom padding extends the background without moving the pills.
    lv_obj_set_style_pad_bottom(bar, 49, 0);
    const char *names[] = {LV_SYMBOL_SHUFFLE "  Jog", LV_SYMBOL_FILE "  Files", LV_SYMBOL_LIST "  Macros", LV_SYMBOL_EYE_OPEN "  Diag", LV_SYMBOL_SETTINGS "  Settings"};
    for (int i = 0; i < 5; ++i) {
        lv_label_set_text(lv_obj_get_child(nav[i], 0), i == 0 && actuator_mode ? LV_SYMBOL_SHUFFLE "  Actuators" : names[i]);
        lv_obj_set_style_bg_color(nav[i], lv_color_hex(CONCEPT_CARD), 0);
        lv_obj_set_style_bg_color(nav[i], lv_color_hex(CONCEPT_INK), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(nav[i], lv_color_hex(CONCEPT_MUTED), 0);
        lv_obj_set_style_text_color(nav[i], lv_color_hex(CONCEPT_CARD), LV_STATE_CHECKED);
        lv_obj_set_style_text_font(nav[i], font(18), 0);
        lv_obj_set_style_text_font(nav[i], font(18), LV_STATE_CHECKED);
        lv_obj_set_style_radius(nav[i], 22, 0);
        lv_obj_set_style_pad_all(nav[i], 0, 0);
        lv_obj_set_height(nav[i], 42);
        auto caption = lv_obj_get_child(nav[i], 0);
        int active = tab_for_page(page);
        lv_obj_set_style_text_color(caption, lv_color_hex(i == active ? CONCEPT_CARD : CONCEPT_MUTED), 0);
        lv_obj_set_style_text_font(caption, font(18), 0);
        lv_obj_set_size(caption, 182, 26);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(caption);
    }
}
static void concept_header() {
    lv_obj_update_layout(root);
    if (concept_header_active()) {
        concept_skin(root, CONCEPT_BG, 0);
        concept_skin(lv_obj_get_child(root, 0), CONCEPT_BG, 0);
    }
    concept_skin(wifi_header, CONCEPT_CARD, 28);
    visibility(brand_label, false);
    concept_place(state_label, 530, 20, 244, 26);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_LEFT, LV_STATE_USER_1);
    concept_text(state_label, CONCEPT_MUTED, font(18));
    // The screen clips the upper corners, leaving a tab attached to the bezel.
    concept_place(wifi_header, 502, -36, 506, 96);
    lv_obj_set_style_bg_opa(wifi_header, LV_OPA_COVER, LV_STATE_USER_1);
    concept_place(wifi_header_name, 288, 44, 148, 24);
    concept_place(wifi_header_ip, 288, 68, 148, 20);
    concept_place(wifi_icon, 454, 52, 40, 36);
}
#include "panel_files_concept.inc"
#include "panel_macros_concept.inc"
#include "panel_fullscreen.inc"
static void concept_settings_controls(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
    } else if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_INK), 0);
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        concept_skin(obj, CONCEPT_PAD, 12);
        concept_text(obj, CONCEPT_INK);
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_MUTED), LV_PART_INDICATOR);
        auto list = lv_dropdown_get_list(obj);
        concept_skin(list, CONCEPT_PAD, 12);
        concept_text(list, CONCEPT_INK);
        lv_obj_set_style_bg_color(list, lv_color_hex(CONCEPT_AXES[2]), LV_PART_SELECTED);
        lv_obj_set_style_text_color(list, lv_color_hex(CONCEPT_CARD), LV_PART_SELECTED);
    } else if (lv_obj_check_type(obj, &lv_buttonmatrix_class)) {
        concept_skin(obj, CONCEPT_PAD, 12);
        for (lv_state_t state : {(lv_state_t)0, (lv_state_t)LV_STATE_CHECKED, (lv_state_t)(LV_STATE_CHECKED | LV_STATE_PRESSED)}) {
            auto part = LV_PART_ITEMS | state;
            lv_obj_set_style_bg_color(obj, lv_color_hex(state ? CONCEPT_AXES[2] : CONCEPT_PAD), part);
            lv_obj_set_style_text_color(obj, lv_color_hex(state ? CONCEPT_CARD : CONCEPT_MUTED), part);
            lv_obj_set_style_border_width(obj, 0, part);
            lv_obj_set_style_shadow_width(obj, 0, part);
        }
    } else if (lv_obj_check_type(obj, &lv_led_class)) {
        return;
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_INK), LV_PART_KNOB);
    } else {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_CARD), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(CONCEPT_PAD), 0);
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i)
        concept_settings_controls(lv_obj_get_child(obj, i));
    if (lv_obj_check_type(obj, &lv_button_class) && lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(obj, 0), lv_color_hex(CONCEPT_CARD), 0);
    }
}
static void concept_settings_layout() {
    page_title = label(root, page == DiagnosticsPage ? "Diagnostics" : "Settings", 40, 20, 440, 28, CONCEPT_INK);
    concept_place(right_surface, 16, 76, 992, 450);
    concept_skin(right_surface, CONCEPT_CARD, 28);
    visibility(right_surface, true);
    concept_place(body, 28, 94, 968, 414);
    concept_skin(body, CONCEPT_CARD, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_STATE_USER_1);
    for (unsigned i = 0; i < lv_obj_get_child_count(body); ++i) {
        auto row = lv_obj_get_child(body, i);
        if (page == SettingsPage) lv_obj_set_y(row, lv_obj_get_y(row) - 24);
        concept_settings_controls(row);
    }
}
static void concept_aux_layout() {
    page_title = label(root, page == JobPage ? "Running job" : "Actuators", 40, 20, 440, 28, CONCEPT_INK);
    if (page == JogPage) {
        concept_place(body,16,76,992,450);
        lv_obj_set_style_bg_opa(body,LV_OPA_TRANSP,0);
        for (unsigned i=0;i<lv_obj_get_child_count(body);++i) {
            auto card=lv_obj_get_child(body,i); form_control_style(card);
            lv_obj_set_style_radius(card,28,0); lv_obj_set_height(card,450);
            if(lv_obj_get_child_count(card)) lv_obj_set_style_text_color(lv_obj_get_child(card,0),lv_color_hex(i<3?CONCEPT_AXES[i]:AMBER),0);
        }
        for(auto slider:act_slider) if(slider) {
            lv_obj_set_style_bg_color(slider,lv_color_hex(CONCEPT_PAD),0);
            lv_obj_set_style_bg_color(slider,lv_color_hex(CONCEPT_PAD),LV_PART_INDICATOR);
        }
        return;
    }
    concept_place(left_surface,16,76,470,450); concept_skin(left_surface,CONCEPT_CARD,28);
    concept_place(right_surface,502,76,506,450); concept_skin(right_surface,CONCEPT_CARD,28);
    visibility(machine_heading,false); visibility(work_heading,false);
    for(int i=0;i<3;++i) {
        concept_place(axis_card[i],32,96+i*100,438,100); concept_skin(axis_card[i],CONCEPT_CARD,0);
        concept_place(axis_name[i],16,14,40,60); concept_text(axis_name[i],CONCEPT_AXES[i],&panel_dro_52);
        concept_place(axis_work[i],80,14,342,60); concept_text(axis_work[i],CONCEPT_INK,&panel_dro_52);
        concept_place(axis_machine[i],184,76,238,22); concept_text(axis_machine[i],CONCEPT_MUTED,font(16));
    }
    concept_place(job_controls,32,432,212,80); lv_obj_set_width(job_pause,212);
    concept_place(stop_btn,260,432,210,80);
    concept_place(body,526,100,466,414); form_control_style(body);
    lv_obj_set_width(file_title,466);
    form_control_style(job_pause); form_control_style(stop_btn);
}
static void concept_layout() {
    concept_dock();
    if (!concept_header_active()) return;
    concept_header();
    if (page == FilesPage) { concept_files_layout(); return; }
    if (page == MacrosPage) { concept_macros_layout(); return; }
    if (page == SettingsPage || page == DiagnosticsPage) { concept_settings_layout(); return; }
    if (page == JobPage || (page == JogPage && actuator_mode)) { concept_aux_layout(); return; }
    if (!concept_active()) return;
    concept_place(left_surface, 16, 16, 470, 510);
    concept_place(right_surface, 502, 76, 506, 450);
    concept_skin(left_surface, CONCEPT_CARD, 28); concept_skin(right_surface, CONCEPT_CARD, 28);
    concept_place(work_heading, 40, 40, 240, 20);
    concept_text(work_heading, CONCEPT_MUTED, font(14));
    lv_label_set_text(work_heading, "WORK POSITION");
    lv_obj_set_style_text_align(work_heading, LV_TEXT_ALIGN_LEFT, LV_STATE_USER_1);
    visibility(machine_heading, false);
    for (int i = 0; i < 3; ++i) {
        auto row = axis_card[i]; concept_place(row, 32, 62 + i * 102, 438, 102);
        concept_skin(row, CONCEPT_CARD, 0);
        concept_place(axis_home[i], 0, 0, 68, 68);
        lv_label_set_text(lv_obj_get_child(axis_home[i], 0), LV_SYMBOL_HOME);
        concept_button(axis_home[i]);
        concept_place(axis_name[i], 80, 14, 32, 60); concept_text(axis_name[i], CONCEPT_AXES[i], &panel_dro_52);
        concept_place(axis_work[i], 110, 14, 212, 60); concept_text(axis_work[i], CONCEPT_INK, &panel_dro_52);
        concept_place(axis_machine[i], 110, 62, 212, 20); concept_text(axis_machine[i], CONCEPT_MUTED, font(14));
        concept_place(axis_zero[i], 334, 0, 104, 68); concept_button(axis_zero[i]);
    }
    concept_place(dro_actions, 32, 378, 438, 62);
    lv_obj_t *actions[] = {home_btn, goto_buttons[0], goto_buttons[1], zero_all_btn};
    const char *titles[] = {"Home all", "XY zero", "Z zero", "Zero all"};
    const char *icons[] = {LV_SYMBOL_HOME, "\xEF\x81\x9B", "\xEF\x81\xA3", "\xEF\x8F\x85"};
    for (int i = 0; i < 4; ++i) {
        concept_place(actions[i], i * 110, 0, 104, 62);
        lv_label_set_text(lv_obj_get_child(actions[i], 0), titles[i]); concept_button(actions[i]);
        concept_action_icon(actions[i], i, icons[i]);
    }
    concept_place(jog_tools, 32, 452, 438, 60);
    concept_place(probe_btn, 0, 0, 132, 60); concept_button(probe_btn);
    concept_place(accessories_btn, 140, 0, 166, 60); concept_button(accessories_btn);
    lv_label_set_text(lv_obj_get_child(accessories_btn, 0), "Tools");
    concept_action_icon(probe_btn, 4, "\xEF\x95\x88");
    concept_action_icon(accessories_btn, 5, "\xEF\x82\xAD");
    concept_place(wcs_dropdown, 314, 0, 124, 60); concept_skin(wcs_dropdown, CONCEPT_PAD); concept_text(wcs_dropdown, CONCEPT_INK, font(16));
    concept_place(body, 526, 100, 466, 414);
    for (unsigned i = 0; i < 2; ++i) visibility(lv_obj_get_child(body, i), false);
    label(body, "JOGGING", 0, 0, 220, 14, CONCEPT_MUTED);
    concept_place(mode_choices, 0, 30, 466, 48, false);
    concept_place(speed_choices, 0, 90, 252, 48, false);
    concept_place(step_dropdown, 270, 90, 196, 48, false);
    concept_skin(step_dropdown, CONCEPT_PAD); concept_text(step_dropdown, CONCEPT_INK, font(16));
    for (unsigned i = 0; i < jog_count; ++i) {
        int cell = i < 4 ? i : i + 1;
        concept_place(jog_buttons[i], i < 8 ? cell % 3 * 108 : 368,
                      i < 8 ? 152 + cell / 3 * 88 : 152 + (i - 8) * 132,
                      98, i < 8 ? 78 : 122, false);
        concept_button(jog_buttons[i], CONCEPT_PAD, CONCEPT_AXES[2]);
        if (i < 8) {
            auto line = lv_obj_get_child(jog_buttons[i], 0);
            uint32_t color = (i == 3 || i == 4) ? CONCEPT_AXES[0] : (i == 1 || i == 6) ? CONCEPT_AXES[1] : CONCEPT_INK;
            lv_obj_set_style_line_color(line, lv_color_hex(color), LV_STATE_USER_1);
        }
    }
}
static void concept_refresh() {
    if (concept_header_active())
        concept_text(state_label, state.alarm || state.error ? 0xFF888D : !strncmp(state.mode, "Hold", 4) ? AMBER : state.status ? 0x6CDB78 : CONCEPT_MUTED, font(18));
    if (page == FilesPage) { concept_files_refresh(); return; }
    if (page == MacrosPage) { concept_macros_refresh(); return; }
    if (!concept_active()) return;
    for (auto matrix : {mode_choices, speed_choices}) {
        if (!matrix) continue;
        concept_skin(matrix, CONCEPT_PAD);
        for (lv_state_t disabled : {(lv_state_t)0, (lv_state_t)LV_STATE_DISABLED}) {
            // Button-matrix cells have their own state, independent of the object USER_1 state.
            auto base = (lv_style_selector_t)((lv_style_selector_t)LV_PART_ITEMS | disabled);
            lv_obj_set_style_bg_color(matrix, lv_color_hex(CONCEPT_PAD), base);
            lv_obj_set_style_bg_opa(matrix, LV_OPA_COVER, base);
            lv_obj_set_style_text_color(matrix, lv_color_hex(disabled ? 0x667277 : CONCEPT_MUTED), base);
            lv_obj_set_style_border_width(matrix, 0, base);
            lv_obj_set_style_shadow_width(matrix, 0, base);
            lv_obj_set_style_radius(matrix, 12, base);
            auto checked = base | LV_STATE_CHECKED;
            lv_obj_set_style_bg_color(matrix, lv_color_hex(disabled ? CONCEPT_PAD : matrix == mode_choices ? CONCEPT_LIME : CONCEPT_INK), checked);
            lv_obj_set_style_text_color(matrix, lv_color_hex(disabled ? 0x667277 : CONCEPT_CARD), checked);
            lv_obj_set_style_border_width(matrix, 0, checked);
            lv_obj_set_style_bg_color(matrix, lv_color_hex(disabled ? CONCEPT_PAD : matrix == mode_choices ? CONCEPT_LIME : CONCEPT_INK), checked | LV_STATE_PRESSED);
            lv_obj_set_style_text_color(matrix, lv_color_hex(disabled ? 0x667277 : CONCEPT_CARD), checked | LV_STATE_PRESSED);
        }
    }
    for (auto o : {wcs_dropdown, step_dropdown})
        lv_obj_set_style_text_color(o, lv_color_hex(CONCEPT_MUTED), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_USER_1);
    // Dim the whole control, including the arrow, when continuous mode disables distance.
    lv_obj_set_style_opa(step_dropdown, LV_OPA_40, LV_STATE_USER_1 | LV_STATE_DISABLED);
    lv_obj_set_style_opa(step_dropdown, LV_OPA_COVER, LV_STATE_USER_1);
    for (int i = 0; i < 3; ++i) {
        concept_text(axis_work[i], state.status ? CONCEPT_INK : CONCEPT_MUTED, &panel_dro_52);
        for (auto control : {axis_home[i], axis_zero[i]})
            concept_text(lv_obj_get_child(control, 0), lv_obj_has_state(control, LV_STATE_DISABLED) ? 0x667277 : CONCEPT_INK, font(control == axis_home[i] ? 22 : 16));
        if (state.coordinates && state.wco_known) {
            char text[32]; snprintf(text, sizeof text, "M  % .3f", state.machine[i] / display_scale(inches));
            lv_label_set_text(axis_machine[i], text);
        }
    }
    for (auto o : {probe_btn, accessories_btn, wcs_dropdown, step_dropdown, home_btn, goto_buttons[0], goto_buttons[1], zero_all_btn}) {
        bool disabled = lv_obj_has_state(o, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(o, lv_color_hex(disabled ? 0x181D20 : CONCEPT_PAD), LV_STATE_USER_1 | LV_STATE_DISABLED);
        if (lv_obj_check_type(o, &lv_button_class) && lv_obj_get_child_count(o))
            concept_text(lv_obj_get_child(o, 0), disabled ? 0x667277 : CONCEPT_INK, font(16));
    }
    lv_obj_t *icon_buttons[] = {home_btn, goto_buttons[0], goto_buttons[1], zero_all_btn, probe_btn, accessories_btn};
    for (unsigned i = 0; i < 6; ++i)
        if (concept_action_icons[i]) concept_text(concept_action_icons[i],
            lv_obj_has_state(icon_buttons[i], LV_STATE_DISABLED) ? 0x667277 : CONCEPT_INK, &panel_action_icons_22);
    for (unsigned i = 0; i < jog_count; ++i) {
        bool disabled = lv_obj_has_state(jog_buttons[i], LV_STATE_DISABLED);
        auto child = lv_obj_get_child(jog_buttons[i], 0);
        if (i < 8) lv_obj_set_style_line_color(child, lv_color_hex(disabled ? 0x667277 : (i == 3 || i == 4) ? CONCEPT_AXES[0] : (i == 1 || i == 6) ? CONCEPT_AXES[1] : CONCEPT_INK), LV_STATE_USER_1);
        else concept_text(child, disabled ? 0x667277 : CONCEPT_AXES[2], font(22));
    }
}
