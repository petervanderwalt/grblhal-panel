// Files shares the Jog palette and the existing SD actions and pagination.
static void concept_files_style_rows() {
    for (unsigned slot = 0; slot < lv_obj_get_child_count(file_box); ++slot) {
        auto row = lv_obj_get_child(file_box, slot);
        if (lv_obj_check_type(row, &lv_label_class)) {
            concept_text(row, CONCEPT_MUTED, font(18));
            continue;
        }
        concept_place(row, 0, slot * 88, 968, 82, false);
        concept_skin(row, CONCEPT_CARD, 0);
        lv_obj_set_style_border_width(row, 1, LV_STATE_USER_1);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_STATE_USER_1);
        lv_obj_set_style_border_color(row, lv_color_hex(CONCEPT_PAD), LV_STATE_USER_1);
        auto name = lv_obj_get_child(row, 0), size = lv_obj_get_child(row, 1);
        concept_place(name, 72, 12, 574, 30, false); concept_text(name, CONCEPT_INK, font(20));
        concept_place(size, 72, 46, 540, 22, false); concept_text(size, CONCEPT_MUTED, font(14));
        unsigned action_count = lv_obj_get_child_count(row);
        if (storage_source()) {
            auto preview = lv_obj_get_child(row, 2);
            concept_place(preview, 676, 11, 124, 60, false);
            concept_button(preview, CONCEPT_PAD, lv_obj_has_state(preview, LV_STATE_DISABLED) ? CONCEPT_MUTED : CONCEPT_AXES[2]);
        }
        auto run = lv_obj_get_child(row, action_count - 1);
        concept_place(run, 812, 11, 132, 60, false);
        lv_label_set_text(lv_obj_get_child(run, 0), LV_SYMBOL_PLAY "  Run");
        concept_button(run, CONCEPT_AXES[1], CONCEPT_CARD);
        label(row, LV_SYMBOL_FILE, 20, 26, 36, 28, CONCEPT_MUTED);
    }
}
static void concept_files_layout() {
    concept_place(body, 16, file_box ? 16 : 76, 992, file_box ? 510 : 450);
    concept_skin(body, CONCEPT_CARD, 28);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_STATE_USER_1);
    if (file_box) {
        // The first two labels are the old table headings; names and sizes now share a row.
        for (unsigned i = 0; i < lv_obj_get_child_count(body); ++i) {
            auto o = lv_obj_get_child(body, i);
            if (lv_obj_check_type(o, &lv_label_class) && o != file_page_label && o != file_prepare_status && o != file_prepare_name)
                visibility(o, false);
        }
        label(body, "Files", 24, 4, 90, 28, CONCEPT_INK);
        concept_place(file_box, 12, 80, 968, 360, false);
        concept_skin(file_box, CONCEPT_CARD, 0);
        lv_obj_remove_flag(file_box, LV_OBJ_FLAG_SCROLLABLE);
        concept_place(refresh_btn, storage_source() ? 112 : 338, 2, 132, 48, false);
        lv_label_set_text(lv_obj_get_child(refresh_btn, 0), LV_SYMBOL_REFRESH "  Refresh");
        concept_button(refresh_btn);
        concept_place(file_previous, 24, 456, 132, 44, false);
        concept_place(file_next, 836, 456, 132, 44, false);
        concept_button(file_previous); concept_button(file_next);
        lv_label_set_text(lv_obj_get_child(file_previous, 0), LV_SYMBOL_LEFT "  Previous");
        lv_label_set_text(lv_obj_get_child(file_next, 0), "Next  " LV_SYMBOL_RIGHT);
        concept_place(file_page_label, 336, 468, 320, 24, false);
        concept_text(file_page_label, CONCEPT_MUTED, font(14));
        if (storage_source()) {
            auto update=button(body,"Update previews",256,2,214,48,show_preview_update);
            concept_button(update,CONCEPT_PAD,CONCEPT_AXES[2]);
        }
        visibility(file_prepare_spinner,false); visibility(file_prepare_status,false); visibility(file_prepare_name,false);
        if (!state.file_count) { enable(file_previous, false); enable(file_next, false); }
        concept_library_card();
    } else {
        // Preview bitmap stays white; the frame and controls follow the library card.
        for (unsigned i = 0; i < lv_obj_get_child_count(body); ++i) {
            auto o = lv_obj_get_child(body, i);
            if (lv_obj_check_type(o, &lv_label_class)) concept_text(o, CONCEPT_INK, font(20));
            if (lv_obj_check_type(o, &lv_button_class)) concept_button(o);
        }
        if (start_btn) {
            lv_label_set_text(lv_obj_get_child(start_btn, 0), LV_SYMBOL_PLAY "  Run from SD");
            concept_button(start_btn, CONCEPT_AXES[1], CONCEPT_CARD);
        }
        if (local_preview_show) {
            concept_skin(cached_text_panel, 0x14191D, 0);
            for (unsigned i = 0; i < lv_obj_get_child_count(cached_text_panel); ++i)
                concept_text(lv_obj_get_child(cached_text_panel, i), i ? CONCEPT_INK : CONCEPT_MUTED, font(i ? 16 : 14));
            concept_text(cached_status, CONCEPT_MUTED, font(14));
            concept_skin(cached_view_dropdown, CONCEPT_PAD);
            concept_text(cached_view_dropdown, CONCEPT_INK, font(16));
            lv_obj_set_style_text_color(cached_view_dropdown, lv_color_hex(CONCEPT_MUTED), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_USER_1);
            auto list = lv_dropdown_get_list(cached_view_dropdown);
            lv_obj_set_style_bg_color(list, lv_color_hex(CONCEPT_PAD), 0);
            lv_obj_set_style_text_color(list, lv_color_hex(CONCEPT_INK), 0);
            lv_obj_set_style_border_width(list, 0, 0);
            lv_obj_set_style_radius(list, 16, 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(CONCEPT_AXES[2]), LV_PART_SELECTED);
            lv_obj_set_style_text_color(list, lv_color_hex(CONCEPT_CARD), LV_PART_SELECTED);
            lv_obj_set_style_arc_color(cached_spinner, lv_color_hex(CONCEPT_PAD), 0);
            lv_obj_set_style_arc_color(cached_spinner, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(cached_spinner, 3, 0);
            lv_obj_set_style_arc_width(cached_spinner, 3, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(cached_bar, lv_color_hex(CONCEPT_PAD), 0);
            lv_obj_set_style_bg_color(cached_bar, lv_color_hex(CONCEPT_AXES[2]), LV_PART_INDICATOR);
        }
    }
}
static void concept_files_refresh() {
    for (auto b : {refresh_btn, file_previous, file_next, start_btn,
                   file_run_buttons[0], file_run_buttons[1], file_run_buttons[2], file_run_buttons[3]}) {
        if (!b) continue;
        bool disabled = lv_obj_has_state(b, LV_STATE_DISABLED);
        bool run = b == start_btn;
        for (auto button : file_run_buttons) run |= button && b == button;
        lv_obj_set_style_bg_color(b, lv_color_hex(0x181D20), LV_STATE_USER_1 | LV_STATE_DISABLED);
        concept_text(lv_obj_get_child(b, 0), disabled ? 0x667277 : run ? CONCEPT_CARD : CONCEPT_INK, font(16));
    }
}
