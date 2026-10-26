// Display-SD preview and server configuration, sharing the existing modal controls.
static unsigned cached_view = 6;
static lv_obj_t *cached_spinner = nullptr, *cached_bar = nullptr;
static lv_obj_t *cached_text_panel = nullptr, *cached_view_dropdown = nullptr;
static uint32_t cached_wait_started;
static uint16_t *cached_pixels = nullptr;
static bool cached_loaded = false;
static char cached_image_path[256] = "";
static lv_point_t image_last;
static float image_zoom = 1, image_pinch = 0;
static int image_x = 0, image_y = 0;
static void cached_input(lv_event_t *e) {
    auto code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
        return;
    auto input = lv_event_get_indev(e);
    if (!input)
        return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        image_last = point;
        image_pinch = 0;
        return;
    }
    if (code != LV_EVENT_PRESSING) {
        if (code == LV_EVENT_RELEASED)
            image_pinch = 0;
        return;
    }
    auto touch = bsp_touch_get_handle();
    esp_lcd_touch_point_data_t points[2] = {};
    uint8_t count = 0;
    if (touch && esp_lcd_touch_get_data(touch, points, &count, 2) == ESP_OK && count >= 2) {
        float dx = (int)points[0].x - (int)points[1].x, dy = (int)points[0].y - (int)points[1].y;
        float distance = sqrtf(dx * dx + dy * dy);
        if (image_pinch > 1 && distance > 1) {
            image_zoom = std::clamp(image_zoom * distance / image_pinch, .5f, 16.f);
            lv_image_set_scale(cached_image, (int)(256 * image_zoom));
        }
        image_pinch = distance;
        image_last = point;
        return;
    }
    if (image_pinch) {
        image_pinch = 0;
        image_last = point;
        return;
    }
    image_x = std::clamp<int>(image_x + point.x - image_last.x, -1200, 1200);
    image_y = std::clamp<int>(image_y + point.y - image_last.y, -600, 600);
    lv_obj_set_pos(cached_image, image_x, image_y);
    image_last = point;
}
static void cached_refresh() {
    if (!local_preview_show || !cached_image)
        return;
    char image[256];
    if (storage_preview(preview_path, cached_view, image, sizeof image)) {
        if (strcmp(image, cached_image_path)) {
            snprintf(cached_image_path, sizeof cached_image_path, "%s", image);
            // Give interactive SD reads priority over other files' cache downloads.
            storage_preview_viewing(true);
            cached_loaded = storage_read_image(image, cached_pixels, 740*328*2);
            storage_preview_viewing(cached_loaded);
            if (!cached_loaded) lv_label_set_text(cached_status,"Image unavailable\nShowing file start");
            lv_obj_invalidate(cached_image);
            visibility(cached_image, cached_loaded);
            visibility(cached_status, !cached_loaded);
            visibility(cached_text_panel, !cached_loaded);
            visibility(cached_view_dropdown, cached_loaded);
            visibility(cached_spinner, false);
            visibility(cached_bar, false);
            image_zoom = 1;
            image_x = image_y = 0;
            lv_image_set_scale(cached_image, 256);
            lv_obj_set_pos(cached_image, 0, 0);
        }
    } else {
        storage_preview_viewing(false);
        char status[160];
        storage_status(status, sizeof status);
        int percent;
        bool active = storage_progress(&percent, preview_path);
        if (active) {
            unsigned seconds = (lv_tick_get() - cached_wait_started) / 1000;
            snprintf(image, sizeof image, "%s\n%02u:%02u", status, seconds / 60, seconds % 60);
        } else snprintf(image, sizeof image, "Image unavailable\nShowing file start");
        visibility(cached_spinner, active);
        visibility(cached_bar, active && percent >= 0);
        if (percent >= 0) lv_bar_set_value(cached_bar, percent, LV_ANIM_OFF);
        if (strcmp(lv_label_get_text(cached_status), image))
            lv_label_set_text(cached_status, image);
        visibility(cached_text_panel, true);
        visibility(cached_view_dropdown, false);
        visibility(cached_status, true);
        visibility(cached_image, false);
    }
}
static void cached_view_changed(lv_event_t *e) {
    cached_view = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    *cached_image_path = 0;
    cached_refresh();
}
static void cached_back(lv_event_t *) {
    local_preview_show = false;
    cached_image = nullptr;
    build_page();
}
static void build_cached_preview() {
    auto title = label(body, preview_path, 84, 26, 660, 20);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    navigation_back(body, cached_back);
    auto frame = box(body, 16, 76, 740, 328, CARD);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    cached_wait_started = lv_tick_get();
    cached_text_panel = box(frame, 0, 0, 740, 328, BG);
    lv_obj_set_style_radius(cached_text_panel, 0, 0);
    lv_obj_set_style_border_width(cached_text_panel, 0, 0);
    label(cached_text_panel, "File start", 16, 10, 708, 14, MUTED);
    char head[1536] = {};
    if (!storage_read_head(preview_path, head, sizeof head))
        snprintf(head, sizeof head, "Cannot read file from onboard SD");
    auto source = label(cached_text_panel, head, 16, 38, 708, 16);
    lv_label_set_long_mode(source, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(source, 280);
    lv_obj_set_style_text_line_space(source, 1, 0);
    cached_spinner = lv_spinner_create(body);
    lv_obj_set_pos(cached_spinner, 856, 166);
    lv_obj_set_size(cached_spinner, 36, 36);
    lv_spinner_set_anim_params(cached_spinner, 1000, 220);
    lv_obj_set_style_arc_color(cached_spinner, lv_color_hex(PRIMARY), LV_PART_INDICATOR);
    cached_status = label(body, "Preparing preview...", 772, 214, 204, 14, MUTED);
    lv_label_set_long_mode(cached_status, LV_LABEL_LONG_DOT);
    lv_obj_set_height(cached_status, 100);
    lv_obj_set_style_text_align(cached_status, LV_TEXT_ALIGN_CENTER, 0);
    cached_bar = lv_bar_create(body);
    lv_obj_set_pos(cached_bar, 772, 316);
    lv_obj_set_size(cached_bar, 204, 8);
    lv_obj_set_style_bg_color(cached_bar, lv_color_hex(TRACK), 0);
    lv_obj_set_style_bg_opa(cached_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cached_bar, lv_color_hex(PRIMARY), LV_PART_INDICATOR);
    cached_image = lv_canvas_create(frame);
    cached_pixels=(uint16_t *)heap_caps_aligned_alloc(128,740*328*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(cached_pixels)lv_canvas_set_buffer(cached_image,cached_pixels,740,328,LV_COLOR_FORMAT_RGB565);
    lv_obj_remove_flag(cached_image,LV_OBJ_FLAG_SCROLLABLE);
    visibility(cached_image,false);
    lv_obj_set_pos(cached_image, 0, 0);
    lv_obj_add_flag(cached_image, LV_OBJ_FLAG_CLICKABLE);
    for (auto event : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(cached_image, cached_input, event, nullptr);
    cached_view_dropdown = dropdown(body, "Top\nBottom\nFront\nBack\nLeft\nRight\nIsometric\nPerspective", cached_view, 772, 90, 204,
             cached_view_changed);
    start_btn = button(body, "Run from SD", 772, 330, 204, 64, start_click, 0, GREEN);
    *cached_image_path = 0;
    cached_refresh();
}
static void server_save(lv_event_t *) {
    if (!storage_server(lv_textarea_get_text(server_field))) {
        notify("Enter an HTTP server address, or leave empty for cached previews only.");
        return;
    }
    close_modal();
    build_page();
}
static void show_preview_server(lv_event_t *) {
    auto c = open_modal("Preview server", 1024, 600);
    modal_close_button(c, 1024);
    label(c, "HTTP address", 40, 120, 128, 18, MUTED);
    server_field = lv_textarea_create(c);
    lv_obj_set_pos(server_field, 184, 100);
    lv_obj_set_size(server_field, 416, 64);
    lv_obj_set_style_text_font(server_field, font(20), 0);
    lv_textarea_set_one_line(server_field, true);
    lv_obj_set_height(server_field,64);
    lv_textarea_set_max_length(server_field, 127);
    char url[128];
    storage_server(url, sizeof url);
    lv_textarea_set_text(server_field, url);
    auto keyboard = lv_keyboard_create(c);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 40, 188);
    lv_obj_set_size(keyboard, 944, 380);
    lv_obj_set_style_text_font(keyboard, font(22), LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, server_field);
    button(c, "Save", 800, 100, 184, 64, server_save, 0, GREEN);
    button(c, "Cancel", 616, 100, 168, 64, cancelled, 0, CARD, true);
    concept_form_dialog(c, keyboard, &server_field, 1);
    server_open = true;
}

static void file_location_changed(lv_event_t *e) {
    if (state.sd_active || state.busy) {
        lv_dropdown_set_selected((lv_obj_t *)lv_event_get_target(e), storage_source() ? 0 : 1);
        return;
    }
    if (!storage_source(lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e)) == 0)) {
        lv_dropdown_set_selected((lv_obj_t *)lv_event_get_target(e), storage_source() ? 0 : 1);
        notify("Could not save file location");
        return;
    }
    local_preview_show = preview_show = preview_pending = false;
    runtime_action(Action::List);
    runtime_snapshot(state);
}

static void preview_update_refresh() {
    bool active, busy; unsigned done,total,failed;
    storage_update_state(active,busy,done,total,failed);
    char status[160], path[160], text[240]; storage_status(status,sizeof status,path,sizeof path);
    const char *name=strrchr(path,'/'); name=name?name+1:path;
    lv_label_set_text(preview_update_name,name);
    if (preview_update_cancelled) snprintf(text,sizeof text,busy?"Stopping...":"Update cancelled");
    else snprintf(text,sizeof text,"%s",status);
    lv_label_set_text(preview_update_status,text);
    int percent; storage_progress(&percent);
    lv_bar_set_value(preview_update_bar,percent<0?0:percent,LV_ANIM_OFF);
    snprintf(text,sizeof text,"%u / %u files%s",done,total,active?"":" checked");
    lv_label_set_text(preview_update_overall,text);
    lv_label_set_text(lv_obj_get_child(preview_update_button,0),active?"Cancel":busy?"Stopping...":"OK");
    lv_obj_set_style_bg_color(preview_update_button,lv_color_hex(active?RED:busy?0x22272B:GREEN),0);
    enable(preview_update_button,!busy||active);
    visibility(preview_update_spinner,active||busy);
}
static void preview_update_action(lv_event_t *) {
    bool active,busy; unsigned done,total,failed;
    storage_update_state(active,busy,done,total,failed);
    if (active) { preview_update_cancelled=true; storage_update_cancel(); preview_update_refresh(); }
    else if (!busy) { close_modal(); build_page(); }
}
static void show_preview_update(lv_event_t *) {
    if (!storage_update_start()) { notify("Preview update unavailable. Wait for the SD card and an idle machine."); return; }
    auto c=open_modal("Update previews");

    lv_obj_set_style_radius(c,0,0);
    preview_update_name=label(c,"",40,116,944,24,0xF2F5F4);
    lv_label_set_long_mode(preview_update_name,LV_LABEL_LONG_DOT);
    preview_update_spinner=lv_spinner_create(c); lv_obj_set_pos(preview_update_spinner,40,196); lv_obj_set_size(preview_update_spinner,30,30);
    lv_spinner_set_anim_params(preview_update_spinner,1000,220);
    lv_obj_set_style_arc_color(preview_update_spinner,lv_color_hex(0x34373C),0);
    lv_obj_set_style_arc_color(preview_update_spinner,lv_color_hex(0x78DEEF),LV_PART_INDICATOR);
    preview_update_status=label(c,"",96,200,888,18,0x94A0A5);
    preview_update_bar=lv_bar_create(c); lv_obj_set_pos(preview_update_bar,40,278); lv_obj_set_size(preview_update_bar,944,16);
    lv_obj_set_style_bg_color(preview_update_bar,lv_color_hex(0x34373C),0); lv_obj_set_style_bg_opa(preview_update_bar,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(preview_update_bar,lv_color_hex(0x78DEEF),LV_PART_INDICATOR);
    preview_update_overall=label(c,"",40,332,944,18,0x94A0A5);
    preview_update_button=button(c,"Cancel",660,488,324,64,preview_update_action,0,0x22272B);
    lv_obj_set_style_text_color(lv_obj_get_child(preview_update_button,0),lv_color_hex(0xF2F5F4),0);
    preview_update_cancelled=false; preview_update_open=true; preview_update_refresh(); finish_modal(c);
}
