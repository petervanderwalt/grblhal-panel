// Shared plate geometry and probing defaults, opened from Settings or the probe dialog.
static void probe_offsets_focus(lv_event_t *e) {
    auto field=(lv_obj_t *)lv_event_get_target(e);
    for (auto other:probe_offset_fields) if (other && other!=field) lv_obj_remove_state(other,LV_STATE_FOCUSED);
    lv_obj_add_state(field,LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(probe_offsets_keyboard,field);
}
static void probe_offsets_changed(lv_event_t *) { lv_label_set_text(probe_offsets_error, ""); }
static void probe_offsets_apply(lv_event_t *) {
    float values[5];
    const float saved[] = {prefs.probe.x,prefs.probe.y,prefs.probe.z,prefs.probe.feed,prefs.probe.inset};
    const float minimum[]={.1f,.1f,.1f,25,1}, maximum[]={50,50,50,500,100};
    const char *names[]={"X offset","Y offset","Z thickness","Probe feed","Corner inset"};
    for (unsigned i=0;i<5;++i) {
        const char *text = lv_textarea_get_text(probe_offset_fields[i]);
        char *end;
        values[i] = strtof(text,&end) * display_scale(inches);
        if (!strcmp(text,probe_offsets_initial[i])) values[i] = saved[i];
        if (!*text || *end || !isfinite(values[i]) || values[i]<minimum[i] || values[i]>maximum[i]) {
            char error[100];
            snprintf(error,sizeof error,"%s must be between %.*f and %.*f %s", names[i],
                     inches?3:1,minimum[i]/display_scale(inches),inches?3:1,maximum[i]/display_scale(inches),
                     i==3 ? (inches?"in/min":"mm/min") : (inches?"in":"mm"));
            lv_label_set_text(probe_offsets_error,error); return;
        }
    }
    prefs.probe.x=values[0]; prefs.probe.y=values[1]; prefs.probe.z=values[2];
    prefs.probe.feed=values[3]; prefs.probe.inset=values[4];
    save(); close_modal(); build_page();
    if (probe_return) { probe_return=false; show_probe(); }
}
static void show_probe_offsets(lv_event_t *) {
    if (state.probing) { notify("Wait for the probe sequence to finish before editing geometry."); return; }
    probe_return = probe_status != nullptr;
    auto card=open_modal("Probe configuration");
    lv_obj_set_width(lv_obj_get_child(card,0),600);
    auto keypad_card=box(card,632,76,376,508);
    lv_obj_set_style_radius(keypad_card,28,0);
    lv_obj_set_style_border_width(keypad_card,0,0);
    modal_close_button(card);
    const float values[]={prefs.probe.x,prefs.probe.y,prefs.probe.z,prefs.probe.feed,prefs.probe.inset};
    const char *names[]={"X offset","Y offset","Z thickness","Probe feed","Corner inset"};
    for (unsigned i=0;i<5;++i) {
        int y=100+i*76;
        label(card,names[i],40,y+8,200,22,TEXT);
        label(card,i==3?(inches?"in/min":"mm/min"):(inches?"in":"mm"),40,y+36,200,14,MUTED);
        auto field=probe_offset_fields[i]=lv_textarea_create(card);
        lv_obj_set_pos(field,264,y); lv_obj_set_size(field,328,64);
        lv_textarea_set_one_line(field,true); lv_obj_set_height(field,64); lv_textarea_set_max_length(field,10);
        lv_textarea_set_accepted_chars(field,"0123456789.");
        lv_obj_set_style_text_font(field,font(22),0);
        lv_obj_set_style_text_align(field,LV_TEXT_ALIGN_RIGHT,0);
        snprintf(probe_offsets_initial[i],sizeof probe_offsets_initial[i],"%.*f",inches?3:1,values[i]/display_scale(inches));
        lv_textarea_set_text(field,probe_offsets_initial[i]);
        lv_obj_add_event_cb(field,probe_offsets_focus,LV_EVENT_FOCUSED,nullptr);
    }
    button(card,"Cancel",40,496,268,64,cancelled,0,RED);
    button(card,"Save",324,496,268,64,probe_offsets_apply,0,GREEN);
    probe_offsets_error=label(card,"",40,474,552,14,RED);
    for (auto field:probe_offset_fields) lv_obj_add_event_cb(field,probe_offsets_changed,LV_EVENT_VALUE_CHANGED,nullptr);
    probe_offsets_keyboard=lv_keyboard_create(card);
    lv_obj_set_align(probe_offsets_keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(probe_offsets_keyboard,656,100); lv_obj_set_size(probe_offsets_keyboard,328,460);
    static const char *const keys[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n",".","0",LV_SYMBOL_BACKSPACE,""};
    static constexpr auto digit = (lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    static const lv_buttonmatrix_ctrl_t controls[]={digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,
        (lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_CHECKED)};
    lv_keyboard_set_map(probe_offsets_keyboard,LV_KEYBOARD_MODE_NUMBER,keys,controls);
    lv_keyboard_set_mode(probe_offsets_keyboard,LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(probe_offsets_keyboard,probe_offset_fields[0]);
    probe_offsets_open=true;
    finish_modal(card);
    lv_obj_set_style_text_font(probe_offsets_keyboard,font(28),LV_PART_ITEMS);
    lv_obj_send_event(probe_offset_fields[0],LV_EVENT_FOCUSED,nullptr);
}
