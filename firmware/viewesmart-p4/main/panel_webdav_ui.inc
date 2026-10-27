// WebDAV activity uses the existing full-screen card treatment, never a floating overlay.
static lv_obj_t *dav_toggle,*dav_address,*dav_action,*dav_path,*dav_bar,*dav_amount,*dav_spinner;
static const char *dav_finished(const WebdavActivity &a) {
    if(a.failed)return "SD transfer failed";
    if(!strcmp(a.action,"Uploading"))return "Uploaded";
    if(!strcmp(a.action,"Downloading"))return "Downloaded";
    if(!strcmp(a.action,"Deleting"))return "Deleted";
    if(!strcmp(a.action,"Copying"))return "Copied";
    if(!strcmp(a.action,"Renaming"))return "Renamed";
    return "Folder created";
}
static void dav_header(char *text,size_t size) {
    if(state.alarm || state.error || state.sd_active)return;
    WebdavActivity a;webdav_activity(a);
    uint32_t stamp=esp_timer_get_time()/1000;
    if(!a.started || (!a.active && stamp-a.updated>3000))return;
    if(a.active && a.total)snprintf(text,size,"%s %u%%",a.action,(unsigned)(100ull*a.done/a.total));
    else snprintf(text,size,"%s",a.active?a.action:dav_finished(a));
}
static void dav_settings_refresh() {
    if(!dav_toggle)return;
    const char *caption=webdav_enabled()?"Disable":"Enable";
    auto text=lv_obj_get_child(dav_toggle,0);
    if(strcmp(lv_label_get_text(text),caption))lv_label_set_text(text,caption);
    char address[100];
    if(webdav_enabled() && wifi.connected)snprintf(address,sizeof address,"http://%s:47820/sd/",wifi.ip);
    else snprintf(address,sizeof address,"%s",webdav_enabled()?"Connect Wi-Fi to share the SD card":"Disabled");
    if(strcmp(lv_label_get_text(dav_address),address))lv_label_set_text(dav_address,address);
    tools_enable(dav_toggle,!storage_remote_active());
}
static void dav_changed(lv_event_t *) {
    if(!webdav_enabled(!webdav_enabled())){notify("Could not change SD card sharing; wait for the transfer to finish.");return;}
    dav_settings_refresh();
}
static bool dav_refresh() {
    WebdavActivity a;webdav_activity(a);uint32_t stamp=esp_timer_get_time()/1000;
    if(!webdav_open && a.active && stamp-a.started>=750 && !modal && !state.sd_active) {
        auto card=open_modal("SD card transfer");webdav_open=true;
        dav_action=label(card,"",56,124,912,28);
        dav_path=label(card,"",56,186,912,22,MUTED);lv_label_set_long_mode(dav_path,LV_LABEL_LONG_DOT);
        dav_bar=lv_bar_create(card);lv_obj_set_pos(dav_bar,56,270);lv_obj_set_size(dav_bar,912,18);
        lv_obj_set_style_bg_opa(dav_bar,LV_OPA_COVER,0);
        dav_amount=label(card,"",56,320,912,22,MUTED);
        dav_spinner=lv_spinner_create(card);lv_obj_set_pos(dav_spinner,56,264);lv_obj_set_size(dav_spinner,32,32);
        lv_spinner_set_anim_params(dav_spinner,1000,220);finish_modal(card);
    }
    if(!webdav_open)return false;
    if(!a.active && stamp-a.updated>=1200) {close_modal();if(page==FilesPage)build_page();return false;}
    auto set=[](lv_obj_t *o,const char *text){if(strcmp(lv_label_get_text(o),text))lv_label_set_text(o,text);};
    set(dav_action,a.active?a.action:dav_finished(a));set(dav_path,a.path);
    visibility(dav_bar,a.total>0);visibility(dav_spinner,!a.total && a.active);
    if(a.total)lv_bar_set_value(dav_bar,std::min(100u,(unsigned)(100ull*a.done/a.total)),LV_ANIM_OFF);
    char amount[96];
    if(a.total)snprintf(amount,sizeof amount,"%.1f / %.1f MB   /   %u%%",a.done/1048576.f,a.total/1048576.f,(unsigned)(100ull*a.done/a.total));
    else if(a.done)snprintf(amount,sizeof amount,"%.1f MB",a.done/1048576.f);
    else snprintf(amount,sizeof amount,"%s",a.active?"":"Complete");
    set(dav_amount,amount);return true;
}
