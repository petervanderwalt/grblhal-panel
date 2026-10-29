// Full-screen secondary pages share the main screen palette and native cards.
static void form_control_style(lv_obj_t *obj) {
    lv_obj_set_style_color_filter_opa(obj,LV_OPA_TRANSP,LV_STATE_DISABLED);
    const bool button = lv_obj_check_type(obj, &lv_button_class);
    uint32_t fill = CONCEPT_PAD, ink = CONCEPT_INK;
    if (button) {
        auto color = lv_color_to_u32(lv_obj_get_style_bg_color(obj, 0)) & 0xFFFFFF;
        const char *text = lv_obj_get_child_count(obj) ? lv_label_get_text(lv_obj_get_child(obj, 0)) : "";
        bool positive = color == GREEN || color == CONCEPT_AXES[1];
        bool danger = color == RED || (!strcmp(text,"Cancel") && !lv_obj_has_state(obj, LV_STATE_USER_2)) || !strcmp(text,"Delete") || !strcmp(text,"Stop") || !strcmp(text,"Disconnect");
        fill = danger ? RED : positive ? CONCEPT_AXES[1] : color == AMBER ? AMBER : CONCEPT_PAD;
        ink = (positive || color == AMBER) && !danger ? CONCEPT_CARD : CONCEPT_INK;
        if (wifi_list && lv_obj_get_parent(obj) == wifi_list) fill = CONCEPT_CARD;
        lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x3B4449), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x171C20), LV_STATE_DISABLED);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_STATE_DISABLED);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, wifi_list && lv_obj_get_parent(obj) == wifi_list ? 0 : 16, 0);
        lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, 0);
    } else if (lv_obj_check_type(obj, &lv_label_class)) {
        auto old = lv_color_to_u32(lv_obj_get_style_text_color(obj, 0)) & 0xFFFFFF;
        uint32_t color = old == MUTED ? CONCEPT_MUTED : old == RED ? CONCEPT_AXES[0] :
                         old == GREEN ? CONCEPT_AXES[1] : old == PRIMARY ? CONCEPT_AXES[2] :
                         old == TEXT ? CONCEPT_INK : old;
        lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    } else if (lv_obj_check_type(obj, &lv_textarea_class) || lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_radius(obj, 12, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_INK), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_MUTED), LV_PART_INDICATOR);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0x39444B), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_PART_CURSOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x171C20), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_MUTED), LV_STATE_DISABLED);
        if (lv_obj_check_type(obj,&lv_textarea_class)) {
            auto type=lv_obj_get_style_text_font(obj,0);
            if(type->line_height<20) { lv_obj_set_style_text_font(obj,font(22),0); type=font(22); }
            int height=lv_obj_get_style_height(obj,0);
            if(lv_textarea_get_one_line(obj) && height>=54 && height<=80)
                lv_obj_set_style_pad_ver(obj,(height-type->line_height-2)/2,0);
        }
        if (lv_obj_check_type(obj, &lv_dropdown_class)) {
            auto list = lv_dropdown_get_list(obj);
            lv_obj_set_style_bg_color(list, lv_color_hex(CONCEPT_PAD), 0);
            lv_obj_set_style_text_color(list, lv_color_hex(CONCEPT_INK), 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(CONCEPT_AXES[2]), LV_PART_SELECTED);
            lv_obj_set_style_text_color(list, lv_color_hex(CONCEPT_CARD), LV_PART_SELECTED);
        }
    } else if (lv_obj_check_type(obj, &lv_keyboard_class) || lv_obj_check_type(obj, &lv_buttonmatrix_class)) {
        concept_keyboard_style(obj);
        if (lv_obj_check_type(obj, &lv_keyboard_class)) {
            lv_obj_set_style_text_font(obj, font(22), LV_PART_ITEMS);
            for (auto state : {(lv_state_t)LV_STATE_CHECKED, (lv_state_t)(LV_STATE_CHECKED | LV_STATE_PRESSED)}) {
                lv_obj_set_style_bg_color(obj,lv_color_hex(state == LV_STATE_CHECKED ? 0x30383D : 0x465157),LV_PART_ITEMS | state);
                lv_obj_set_style_text_color(obj,lv_color_hex(CONCEPT_INK),LV_PART_ITEMS | state);
            }
        }
        for (auto state : {(lv_state_t)LV_STATE_DISABLED, (lv_state_t)(LV_STATE_DISABLED | LV_STATE_CHECKED)}) {
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x171C20), LV_PART_ITEMS | state);
            lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_MUTED), LV_PART_ITEMS | state);
        }
    } else if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_INK), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(obj, lv_color_hex(CONCEPT_MUTED), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(obj, lv_color_hex(CONCEPT_MUTED), LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x171C20), LV_PART_INDICATOR | LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x171C20), LV_PART_INDICATOR | LV_STATE_DISABLED | LV_STATE_CHECKED);
    } else if (lv_obj_check_type(obj, &lv_bar_class) || lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_MUTED), LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_PAD), LV_PART_INDICATOR | LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(MUTED), LV_PART_KNOB | LV_STATE_DISABLED);
    } else if ((lv_obj_check_type(obj, &lv_spinner_class) || lv_obj_check_type(obj, &lv_arc_class))) {
        lv_obj_set_style_arc_color(obj, lv_color_hex(CONCEPT_PAD), 0);
        lv_obj_set_style_arc_color(obj, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR);
    } else if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CONCEPT_CARD), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(CONCEPT_PAD), 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i) form_control_style(lv_obj_get_child(obj,i));
    if (button && lv_obj_get_child_count(obj)) {
        bool disabled = lv_obj_has_state(obj, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(lv_obj_get_child(obj,0), lv_color_hex(disabled ? CONCEPT_MUTED : lv_obj_has_state(obj,LV_STATE_CHECKED) ? CONCEPT_CARD : ink), 0);
    }
}
static void finish_modal(lv_obj_t *card) {
    bool measure=perf_pending && !ui_perf.built;
    if(measure) perf_mark(ui_perf.create_us);
    if (!modal || modal_plain) { if(measure) perf_end(); return; }
    // Inset the existing large-form layouts inside the shared page card once.
    if (modal_inset_pending) {
        for (unsigned i = 2; i < lv_obj_get_child_count(card); ++i) {
            auto o = lv_obj_get_child(card,i);
            if (lv_obj_get_y(o) < 64) continue;
            int x = lv_obj_get_x(o) + 20, y = lv_obj_get_y(o) + 16;
            lv_obj_set_pos(o,x,y);
            if (lv_obj_check_type(o,&lv_keyboard_class)) lv_obj_set_width(o,984-x);
            if (x + lv_obj_get_width(o) > 984) lv_obj_set_width(o,984-x);
            if (y + lv_obj_get_height(o) > 564) lv_obj_set_height(o,564-y);
        }
        modal_inset_pending = false;
    }
    for (unsigned i = 1; i < lv_obj_get_child_count(card); ++i) form_control_style(lv_obj_get_child(card,i));
    if(measure) { perf_mark(ui_perf.style_us); perf_end(); }
}
