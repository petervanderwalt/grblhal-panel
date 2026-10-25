static void probe_closed(lv_event_t *) {
    probe_return = false;
    close_modal();
}
// Probe modal shares numeric editing, confirmation and contact gating with Corner.
static void probe_mode_changed(lv_event_t *e) {
    if (state.probing)
        return;
    single_probe = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 0;
    show_probe();
}
static void probe_surface_changed(lv_event_t *e) {
    if (state.probing)
        return;
    probe_surface_choice = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    if (probe_surface_choice > 4)
        probe_surface_choice = 0;
    show_probe();
}
static void show_probe(lv_event_t *) {
    runtime_snapshot(state);
    if (ready() && state.probe_known && !state.contact_ready && !state.probing && state.spindle == 0 &&
        state.spindle_direction == 0)
        runtime_action(Action::Contact);
    auto c = open_modal("Probe", 920, 500);
    modal_close_button(c, 920, probe_closed);
    static const char *const modes[] = {"Single surface", "Corner", ""};
    auto mode = choices(c, modes, single_probe ? 0 : 1, 24, 80, 872, 48, probe_mode_changed);
    enable(mode, !state.probing);
    if (single_probe) {
        auto surface = dropdown(
            c, "Z-  Top surface\nX-  Right surface\nX+  Left surface\nY-  Rear surface\nY+  Front surface",
            probe_surface_choice, 24, 144, 872, probe_surface_changed);
        enable(surface, !state.probing);
    } else {
        const char *names[] = {"Front left", "Front right", "Rear left", "Rear right"};
        for (int i = 0; i < 4; i++)
            enable(button(c, names[i], 24 + i * 218, 144, 206, 48, corner, i,
                          i == prefs.probe.corner ? PRIMARY : EDGE),
                   !state.probing);
    }
    bool z = single_probe && surface_axes[probe_surface_choice] == 2;
    value_button(c,
                 z ? (inches ? "Plate thickness / in" : "Plate thickness / mm")
                   : (inches ? "Tool diameter / in" : "Tool diameter / mm"),
                 z ? prefs.probe.z : prefs.probe.diameter, 24, 212, 410, z ? 5 : 2);
    value_button(c, inches ? "Feed / in/min" : "Feed / mm/min", prefs.probe.feed, 464, 212, 432, 6);
    char text[160];
    snprintf(text, sizeof text, "Plate X %.3f   Y %.3f   Z %.3f %s", prefs.probe.x / display_scale(inches),
             prefs.probe.y / display_scale(inches), prefs.probe.z / display_scale(inches),
             inches ? "in" : "mm");
    if (single_probe) {
        int axis = surface_axes[probe_surface_choice];
        if (axis == 2)
            snprintf(text, sizeof text, "Travel %.3f   Retract %.3f %s", 25 / display_scale(inches),
                     2 / display_scale(inches), inches ? "in" : "mm");
        else
            snprintf(text, sizeof text, "Plate %c offset %.3f   Travel %.3f   Retract %.3f %s", "XYZ"[axis],
                     (axis == 0 ? prefs.probe.x : prefs.probe.y) / display_scale(inches),
                     25 / display_scale(inches), 2 / display_scale(inches), inches ? "in" : "mm");
    }
    label(c, text, 24, 294, 872, 18, MUTED);
    probe_status = label(c, "", 24, 332, 872, 18, AMBER);
    contact_btn = button(c, "Test probe", 24, 404, 410, 64, contact);
    probe_start =
        button(c, single_probe ? "Probe surface" : "Probe corner", 464, 404, 432, 64, probe_click, 0, GREEN);
    refresh(nullptr);
}
