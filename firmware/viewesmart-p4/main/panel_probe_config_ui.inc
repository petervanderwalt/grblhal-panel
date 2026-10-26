// Shared plate geometry and probing defaults, opened from Settings or the probe dialog.
static void probe_offsets_focus(lv_event_t *e) {
    lv_keyboard_set_textarea(probe_offsets_keyboard,(lv_obj_t *)lv_event_get_target(e));
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
    auto card=open_modal(inches?"Probe configuration (in)":"Probe configuration (mm)",1024,600);
    modal_close_button(card,1024);
    const float values[]={prefs.probe.x,prefs.probe.y,prefs.probe.z,prefs.probe.feed,prefs.probe.inset};
    for (unsigned i=0;i<5;++i) {
        if (i<3) {
            char axis[]={"XYZ"[i],0}; label(card,axis,16+i*192,98,24,18,MUTED);
        } else {
            label(card,i==3?"Probe feed":"Corner inset",i==3?16:500,174,156,18,MUTED);
            label(card,i==3?(inches?"in/min":"mm/min"):(inches?"in":"mm"),i==3?368:880,174,116,16,MUTED);
        }
        auto field=probe_offset_fields[i]=lv_textarea_create(card);
        lv_obj_set_pos(field,i<3?44+i*192:i==3?160:672,i<3?76:152); lv_obj_set_size(field,i<3?148:196,64);
        lv_textarea_set_one_line(field,true); lv_textarea_set_max_length(field,10);
        lv_textarea_set_accepted_chars(field,"0123456789.");
        lv_obj_set_style_text_font(field,font(20),0);
        snprintf(probe_offsets_initial[i],sizeof probe_offsets_initial[i],"%.*f",inches?3:1,values[i]/display_scale(inches));
        lv_textarea_set_text(field,probe_offsets_initial[i]);
        lv_obj_add_event_cb(field,probe_offsets_focus,LV_EVENT_FOCUSED,nullptr);
    }
    button(card,"Save",804,76,204,64,probe_offsets_apply,0,GREEN);
    button(card,"Cancel",592,76,196,64,cancelled,0,CARD,true);
    probe_offsets_error=label(card,"",16,220,992,14,RED);
    for (auto field:probe_offset_fields) lv_obj_add_event_cb(field,probe_offsets_changed,LV_EVENT_VALUE_CHANGED,nullptr);
    probe_offsets_keyboard=lv_keyboard_create(card);
    lv_obj_set_align(probe_offsets_keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(probe_offsets_keyboard,8,244); lv_obj_set_size(probe_offsets_keyboard,1008,348);
    lv_obj_set_style_text_font(probe_offsets_keyboard,font(22),LV_PART_ITEMS);
    static const char *const keys[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n",".","0",LV_SYMBOL_BACKSPACE,""};
    static constexpr auto digit = (lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    static const lv_buttonmatrix_ctrl_t controls[]={digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,digit,
        (lv_buttonmatrix_ctrl_t)(1|LV_BUTTONMATRIX_CTRL_CHECKED)};
    lv_keyboard_set_map(probe_offsets_keyboard,LV_KEYBOARD_MODE_NUMBER,keys,controls);
    lv_keyboard_set_mode(probe_offsets_keyboard,LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(probe_offsets_keyboard,probe_offset_fields[0]);
    probe_offsets_open=true;
    concept_form_dialog(card,probe_offsets_keyboard,probe_offset_fields,5);
    lv_obj_set_style_text_color(probe_offsets_error,lv_color_hex(RED),LV_STATE_USER_1);
}
