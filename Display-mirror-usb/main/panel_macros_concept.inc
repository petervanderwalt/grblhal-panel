constexpr uint32_t MACRO_ACCENTS[] = {0x79BFFF, 0x68D9CD, AMBER, CONCEPT_AXES[1]};
static void concept_macros_layout() {
    concept_place(body, 16, 16, 992, 510);
    concept_skin(body, CONCEPT_CARD, 28);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_STATE_USER_1);
    auto title = lv_obj_get_child(body, 0);
    concept_place(title, 24, 4, 300, 38, false); concept_text(title, CONCEPT_INK, font(28));
    concept_place(macro_status, 158, 18, 144, 22, false); concept_text(macro_status, CONCEPT_MUTED, font(12));
    lv_label_set_long_mode(macro_status, LV_LABEL_LONG_DOT);
    concept_place(macro_add_button, 314, 2, 156, 48, false);
    concept_button(macro_add_button, CONCEPT_AXES[1], CONCEPT_CARD);
    concept_place(macro_stop, 314, 2, 156, 48, false); concept_button(macro_stop, RED, WHITE);
    concept_place(macro_list, 12, 80, 968, 414, false); concept_skin(macro_list, CONCEPT_CARD, 0);
    lv_obj_remove_flag(macro_list, LV_OBJ_FLAG_SCROLLABLE);
    unsigned count = 0;
    for (unsigned i = 0; i < MaxMacros; ++i) {
        if (!macro_runs[i]) continue;
        ++count;
        auto b = macro_runs[i]; int x = (i % 4) * 244, y = (i / 4) * 82;
        concept_place(b, x, y, 176, 76, false); concept_button(b);
        concept_skin(b, CONCEPT_PAD, 0);
        lv_obj_set_style_border_width(b, 8, LV_STATE_USER_1);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, LV_STATE_USER_1);
        lv_obj_set_style_border_color(b, lv_color_hex(MACRO_ACCENTS[macros->entries[i].color]), LV_STATE_USER_1);
        auto caption = lv_obj_get_child(b, 0); lv_label_set_text(caption, macros->entries[i].name);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
        lv_obj_set_width(caption, 148);
        lv_obj_set_height(caption, 24);
        concept_place(macro_edits[i], x + 184, y, 48, 76, false); concept_button(macro_edits[i]);
        concept_skin(macro_edits[i], CONCEPT_PAD, 0);
    }
    if (!count) {
        auto symbol = label(macro_list, LV_SYMBOL_LIST, 428, 148, 112, 32, CONCEPT_MUTED);
        lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
        auto text = label(macro_list, "No macros", 284, 198, 400, 18, CONCEPT_MUTED);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    }
    concept_library_card();
}
static void concept_macros_refresh() {
    visibility(macro_add_button, !state.macro_running && !state.macro_failed);
    for (auto b : {macro_add_button, macro_stop}) {
        bool disabled = lv_obj_has_state(b, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x181D20), LV_STATE_USER_1 | LV_STATE_DISABLED);
        concept_text(lv_obj_get_child(b, 0), disabled ? 0x667277 : b == macro_add_button ? CONCEPT_CARD : WHITE, font(16));
    }
    for (unsigned i = 0; i < MaxMacros; ++i) {
        for (auto b : {macro_runs[i], macro_edits[i]}) {
            if (!b) continue;
            bool disabled = lv_obj_has_state(b, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x181D20), LV_STATE_USER_1 | LV_STATE_DISABLED);
            concept_text(lv_obj_get_child(b, 0), disabled ? 0x667277 : b == macro_runs[i] ? CONCEPT_INK : CONCEPT_MUTED, font(16));
        }
    }
}
static void concept_macro_color() {
    unsigned selected = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (lv_buttonmatrix_has_button_ctrl(macro_colors_control, i, LV_BUTTONMATRIX_CTRL_CHECKED)) selected = i;
    lv_obj_set_style_bg_color(macro_colors_control, lv_color_hex(MACRO_ACCENTS[selected]), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(macro_colors_control, lv_color_hex(MACRO_ACCENTS[selected]), LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
}
static void concept_keyboard_style(lv_obj_t *keyboard) {
    concept_skin(keyboard, CONCEPT_CARD, 0);
    lv_obj_set_style_pad_row(keyboard, 6, 0); lv_obj_set_style_pad_column(keyboard, 6, 0);
    for (lv_state_t state : {(lv_state_t)0, (lv_state_t)LV_STATE_PRESSED, (lv_state_t)LV_STATE_CHECKED}) {
        auto part = (lv_style_selector_t)LV_PART_ITEMS | state;
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(state == LV_STATE_CHECKED ? CONCEPT_AXES[2] : state == LV_STATE_PRESSED ? 0x3B4449 : CONCEPT_PAD), part);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(state == LV_STATE_CHECKED ? CONCEPT_CARD : CONCEPT_INK), part);
        lv_obj_set_style_border_width(keyboard, 0, part);
        lv_obj_set_style_shadow_width(keyboard, 0, part);
        lv_obj_set_style_radius(keyboard, 8, part);
    }
}
static void concept_macro_dialog() {
    if (page != MacrosPage || !modal) return;
    auto card = lv_obj_get_child(modal,0);
    finish_modal(card);
    if (!macro_editor_open) return;
    lv_obj_set_style_pad_ver(macro_fields[0],14,0);
    lv_obj_set_height(macro_fields[0],56);
    concept_macro_color();
    lv_obj_add_event_cb(macro_colors_control, [](lv_event_t *) { concept_macro_color(); }, LV_EVENT_VALUE_CHANGED, nullptr);
}
static void concept_form_dialog(lv_obj_t *card, lv_obj_t *keyboard, lv_obj_t *const *fields, unsigned count) {
    if (!modal_plain) { finish_modal(card); return; }
    concept_skin(card, CONCEPT_CARD, 0);
    concept_skin(lv_obj_get_child(card,0), CONCEPT_CARD, 0);
    for (unsigned i=0;i<count;++i) {
        concept_skin(fields[i], CONCEPT_PAD, 12); concept_text(fields[i], CONCEPT_INK);
        lv_obj_set_style_border_width(fields[i],1,LV_STATE_USER_1);
        lv_obj_set_style_border_color(fields[i],lv_color_hex(0x39444B),LV_STATE_USER_1);
        lv_obj_set_style_border_color(fields[i],lv_color_hex(CONCEPT_AXES[2]),LV_STATE_USER_1 | LV_STATE_FOCUSED);
        lv_obj_set_style_pad_ver(fields[i],18,LV_STATE_USER_1);
        lv_obj_set_height(fields[i],64);
        lv_obj_set_style_border_color(fields[i],lv_color_hex(CONCEPT_AXES[2]),LV_PART_CURSOR);
    }
    for (unsigned i=1;i<lv_obj_get_child_count(card);++i) {
        auto child=lv_obj_get_child(card,i);
        if (lv_obj_check_type(child,&lv_label_class)) concept_text(child,CONCEPT_INK);
        if (!lv_obj_check_type(child,&lv_button_class)) continue;
        const char *text=lv_label_get_text(lv_obj_get_child(child,0));
        bool positive = !strcmp(text,"Save") || !strcmp(text,"Apply");
        concept_button(child,positive?CONCEPT_AXES[1]:!strcmp(text,"Cancel")?RED:CONCEPT_PAD,
                       positive?CONCEPT_CARD:CONCEPT_INK);
    }
    concept_keyboard_style(keyboard);
}
