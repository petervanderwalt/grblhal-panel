// LVGL-thread timings; UART reads the completed sample under the display lock.
struct UiPerf {
    unsigned seq=0;
    int page=0;
    char screen[48]="";
    bool built=false,ready=false;
    uint32_t prepare_us=0,clear_us=0,setup_us=0,create_us=0,style_us=0,update_us=0,build_us=0;
    uint32_t layout_us=0,render_us=0,flush_us=0,wait_us=0,refresh_us=0,transition_us=0;
    unsigned flushes=0,pixels=0,heap=0;
};
static UiPerf ui_perf;
static unsigned perf_seq;
static int64_t perf_started,perf_phase,perf_refr,perf_render,perf_flush,perf_wait;
static bool perf_pending;
static void perf_begin(const char *name) {
    ui_perf=UiPerf(); ui_perf.seq=++perf_seq; ui_perf.page=page;
    snprintf(ui_perf.screen,sizeof ui_perf.screen,"%s",name);
    perf_started=perf_phase=esp_timer_get_time(); perf_pending=true;
    perf_refr=perf_render=perf_flush=perf_wait=0;
}
static void perf_mark(uint32_t &value) {
    int64_t stamp=esp_timer_get_time(); value=(uint32_t)(stamp-perf_phase); perf_phase=stamp;
}
static void perf_end() {
    ui_perf.build_us=(uint32_t)(esp_timer_get_time()-perf_started);
    ui_perf.heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL); ui_perf.built=true;
}
static void perf_display(lv_event_t *event) {
    if(!perf_pending || !ui_perf.built) return;
    int64_t stamp=esp_timer_get_time();
    switch(lv_event_get_code(event)) {
    case LV_EVENT_REFR_START: perf_refr=stamp; break;
    case LV_EVENT_RENDER_START: perf_render=stamp; ui_perf.layout_us=(uint32_t)(stamp-perf_refr); break;
    case LV_EVENT_FLUSH_START: {
        perf_flush=stamp; auto area=(const lv_area_t *)lv_event_get_param(event);
        ui_perf.flushes++; if(area) ui_perf.pixels+=lv_area_get_size(area); break;
    }
    case LV_EVENT_FLUSH_FINISH: if(perf_flush) ui_perf.flush_us+=(uint32_t)(stamp-perf_flush); break;
    case LV_EVENT_FLUSH_WAIT_START: perf_wait=stamp; break;
    case LV_EVENT_FLUSH_WAIT_FINISH: if(perf_wait) ui_perf.wait_us+=(uint32_t)(stamp-perf_wait); break;
    case LV_EVENT_RENDER_READY: if(perf_render) ui_perf.render_us=(uint32_t)(stamp-perf_render); break;
    case LV_EVENT_REFR_READY:
        if(perf_render) {
            ui_perf.refresh_us=(uint32_t)(stamp-perf_refr);
            ui_perf.transition_us=(uint32_t)(stamp-perf_started);
            ui_perf.ready=true; perf_pending=false;
        }
        break;
    default: break;
    }
}
static unsigned perf_nodes(lv_obj_t *obj) {
    unsigned n=1;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++) n+=perf_nodes(lv_obj_get_child(obj,i));
    return n;
}
static void perf_report(char *buffer,size_t size) {
    auto j=cJSON_CreateObject();
    if(!j) { snprintf(buffer,size,"{\"error\":\"memory\"}"); return; }
#define PERF_NUM(key) cJSON_AddNumberToObject(j,#key,ui_perf.key)
    PERF_NUM(seq); PERF_NUM(page); cJSON_AddStringToObject(j,"screen",ui_perf.screen);
    cJSON_AddBoolToObject(j,"ready",ui_perf.ready);
    PERF_NUM(prepare_us); PERF_NUM(clear_us); PERF_NUM(setup_us); PERF_NUM(create_us); PERF_NUM(style_us); PERF_NUM(update_us); PERF_NUM(build_us);
    PERF_NUM(layout_us); PERF_NUM(render_us); PERF_NUM(flush_us); PERF_NUM(wait_us); PERF_NUM(refresh_us); PERF_NUM(transition_us);
    PERF_NUM(flushes); PERF_NUM(pixels); PERF_NUM(heap);
#undef PERF_NUM
    cJSON_AddNumberToObject(j,"nodes",perf_nodes(root)+perf_nodes(lv_layer_top()));
    if(!cJSON_PrintPreallocated(j,buffer,size,false)) snprintf(buffer,size,"{\"error\":\"perf buffer\"}");
    cJSON_Delete(j);
}
