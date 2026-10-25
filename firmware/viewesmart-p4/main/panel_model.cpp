#include "panel_model.h"
#include "panel_macros.h"
#include <ctype.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace panel {
static void copy(char *to, const char *from, size_t size) { snprintf(to, size, "%s", from); }
static bool valid_sd_path(const char *path) {
    if (!path || path[0] != '/' || strlen(path) > 159)
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 32 || *p >= 127 || strchr("!~?|]", *p))
            return false;
    return true;
}
static bool vector3(const char *s, float *v) {
    float a, b, c;
    if (sscanf(s, "%f,%f,%f", &a, &b, &c) != 3 || !isfinite(a) || !isfinite(b) || !isfinite(c))
        return false;
    v[0] = a;
    v[1] = b;
    v[2] = c;
    return true;
}
static unsigned vector4(const char *s, float *v) {
    float values[4] = {};
    int n = sscanf(s, "%f,%f,%f,%f", values, values + 1, values + 2, values + 3);
    if (n < 3)
        return 0;
    for (int i = 0; i < n; i++) {
        if (!isfinite(values[i]))
            return 0;
        v[i] = values[i];
    }
    return n;
}
static const char *fault_text(bool alarm, int code) {
    if (alarm) {
        switch (code) {
        case 1:
            return "Hard limit triggered. Check position and re-home.";
        case 2:
            return "Soft limit exceeded. Check travel and work offsets.";
        case 3:
            return "Reset during motion. Position may be lost.";
        case 4:
            return "Probe already triggered before the move.";
        case 5:
            return "Probe did not contact within the search distance.";
        case 8:
            return "Homing failed to clear a limit switch.";
        case 9:
            return "Homing did not find the limit switch.";
        case 10:
            return "Emergency stop asserted. Clear the input and reset.";
        case 11:
            return "Homing required. Clear travel and home the machine.";
        default:
            return "Check controller alarm cause before recovery.";
        }
    }
    switch (code) {
    case 2:
        return "Invalid number in command.";
    case 3:
        return "Command not supported in this context.";
    case 5:
        return "Homing is not enabled on the controller.";
    case 9:
        return "Command locked by alarm or jog state.";
    case 15:
        return "Jog target exceeds machine travel.";
    case 20:
        return "Unsupported G-code command.";
    case 60:
        return "Controller SD card is unavailable.";
    case 79:
        return "Command not available in the current machine state.";
    default:
        return "Controller rejected the command. Check setup before retrying.";
    }
}
Model::Model(Line l, Realtime r, void *c) : line_(l), realtime_(r), context_(c), stream_(l,c) {}
void Model::cancel(const char *message) {
    if (state.local_job) { state.job_failed = true; stream_.cancel(); }
    state.local_job = state.job_preparing = false; stream_paused_ = false;
    if (state.macro_running) state.macro_failed = true;
    if (state.settings_loading || state.settings_saving) {
        state.settings_result = -1;
        snprintf(state.settings_message, sizeof state.settings_message, "%s", message);
        state.settings_revision++;
    }
    state.settings_loading = state.settings_saving = false;
    setting_requested_.clear();
    setting_write_id_ = -1;
    actuator_pending_ = false;
    state.cycle_axis = -1;
    cycle_resume_ = false;
    cycle_speed_pending_ = false;
    if (state.sd_active && (state.alarm || state.error))
        state.job_failed = true;
    goto_pending_ = false;
    state.macro_running = false;
    if (state.downloading)
        state.download_ready = false;
    state.downloading = false;
    for (auto &command : queue_) command.clear();
    queued_ = cursor_ = 0;
    awaiting_ = need_probe_ = got_probe_ = false;
    state.busy = state.probing = state.listing = false;
    if (state.spindles_loading) { state.spindles_result = -1; state.spindle_revision++; }
    state.spindles_loading = false;
    state.contact_ready = false;
    contact_stage_ = 0;
    state.contact_testing = false;
    if (!state.job_seen_run)
        state.sd_active = false;
    copy(state.message, message, sizeof state.message);
}
void Model::begin(uint32_t now) {
    bool interrupted = state.local_job;
    stream_.cancel();
    settings.clear();
    state.~State();
    new (&state) State();
    now_ = now;
    poll_ = now;
    identified_at_ = 0;
    initialized_ = false;
    full_report_ = false;
    cancel("Identifying controller with $I");
    state.online = true;
    if(interrupted) { state.job_failed = true; state.error = 1; }
    copy(state.mode, "Connecting", sizeof state.mode);
    if (!line_(context_, "$I"))
        disconnect();
    sent_ = now;
}
void Model::disconnect() {
    state.homed_axes = 0;
    state.machine_known = false;
    cancel("Controller disconnected");
    state.online = false;
    state.status = false;
    state.identified = false;
    state.download_ready = false;
    copy(state.mode, "Disconnected", sizeof state.mode);
}
bool Model::idle() const {
    return state.online && state.identified && state.status && state.cycle_axis < 0 && !strcmp(state.mode, "Idle") && !state.alarm &&
           !state.error && !state.busy && !state.sd_active && !state.uploading && !state.macro_failed;
}
void Model::transfer_finished(uint32_t now) {
    state.uploading = state.busy = state.status = false;
    state.download_ready = false;
    identified_at_ = now;
    poll_ = now - 250;
}
bool Model::append(const char *s) {
    if (queued_ >= 40 || strlen(s) >= 640 || strchr(s, '\n') || strchr(s, '\r'))
        return false;
    queue_[queued_++] = s;
    state.busy = true;
    return true;
}
bool Model::action(const char *s) {
    if (!idle())
        return false;
    return append(s);
}
bool Model::rt(uint8_t b) { return state.online && realtime_(context_, b); }
bool Model::list() {
    if (!idle())
        return false;
    state.file_count = 0;
    state.file_revision++;
    state.listing = true;
    copy(state.message, "Reading controller SD card", sizeof state.message);
    return append("$F+");
}
bool Model::run(const char *path) {
    if (!idle() || !valid_sd_path(path))
        return false;
    bool found = false;
    for (int i = 0; i < state.file_count; i++)
        if (!state.files[i].directory && !state.files[i].unusable && !strcmp(path, state.files[i].path))
            found = true;
    if (!found)
        return false;
    char cmd[192];
    snprintf(cmd, sizeof cmd, "$F=%s", path);
    copy(state.filename, path, sizeof state.filename);
    state.percent = 0;
    state.job_complete = state.job_end = state.job_failed = false;
    state.elapsed = 0;
    state.job_started = now_;
    state.job_seen_run = false;
    bool ok = append(cmd);
    if (ok)
        state.sd_active = true;
    return ok;
}
bool Model::download(const char *path, Download sink, void *context) {
    if (!idle() || !valid_sd_path(path) || !sink)
        return false;
    const File *file = nullptr;
    for (int i = 0; i < state.file_count; i++)
        if (!state.files[i].directory && !state.files[i].unusable && !strcmp(state.files[i].path, path))
            file = &state.files[i];
    if (!file)
        return false;
    if (file->size > MaxPreviewBytes) {
        copy(state.message, "Preview limit is 2 MiB. Controller SD run remains available.",
             sizeof state.message);
        return false;
    }
    char cmd[192];
    snprintf(cmd, sizeof cmd, "$F<=%s", path);
    if (!append(cmd))
        return false;
    download_sink_ = sink;
    download_context_ = context;
    download_failed_ = false;
    state.downloading = true;
    state.download_ready = false;
    state.download_bytes = 0;
    sent_ = now_;
    state.download_total = file->size;
    copy(state.download_path, path, sizeof state.download_path);
    copy(state.message, "Downloading controller SD preview", sizeof state.message);
    return true;
}
bool Model::jog(char axis, float d, float feed) {
    if (!idle() || !strchr("XYZ", axis) || !axis || !isfinite(d) || !isfinite(feed) || d == 0 ||
        fabsf(d) > 100 || feed < 1 || feed > 10000)
        return false;
    char cmd[96];
    snprintf(cmd, sizeof cmd, "$J=G91 G21 %c%.3f F%.1f", axis, d, feed);
    return append(cmd);
}
bool Model::zero(int axes) {
    if (!idle() || axes < 1 || axes > 7)
        return false;
    char cmd[64] = "G10 L20 P0";
    if (axes & 1) {
        strcat(cmd, " X0");
    }
    if (axes & 2) {
        strcat(cmd, " Y0");
    }
    if (axes & 4) {
        strcat(cmd, " Z0");
    }
    return append(cmd);
}
bool Model::select_wcs(int wcs) {
    if (wcs < 54 || wcs > 59)
        return false;
    char cmd[8];
    snprintf(cmd, sizeof cmd, "G%d", wcs);
    if (!action(cmd))
        return false;
    return append("$G");
}
bool Model::goto_zero(int axes) {
    if (!idle() || !state.coordinates || !state.wco_known || !state.modal_known || (axes != 3 && axes != 4))
        return false;
    const bool absolute = state.absolute;
    char restore_z[64];
    snprintf(restore_z, sizeof restore_z, "G53 G0 Z%.4f", state.machine[2]);
    append("G21 G90");
    if (axes == 3) {
        append("G53 G0 Z-5");
        append("G0 X0");
        append("G0 Y0");
        append(restore_z);
    } else {
        append("G0 Z0");
    }
    append("G21");
    append(absolute ? "G90" : "G91");
    append("$G");
    copy(state.mode, "Run", sizeof state.mode);
    goto_pending_ = true;
    return true;
}
bool Model::jog_vector(const char *directions, float distance, float feed) {
    if (!idle() || !directions || strlen(directions) != 3 || !isfinite(distance) || distance <= 0 ||
        distance > 10000 || !isfinite(feed) || feed < .1f || feed > 1000000)
        return false;
    char cmd[128] = "$J=G91 G21";
    bool moving = false;
    for (int i = 0; i < 3; i++) {
        if (!strchr("-0+", directions[i]))
            return false;
        if (directions[i] == '0')
            continue;
        char word[32];
        snprintf(word, sizeof word, " %c%.3f", "XYZ"[i], directions[i] == '+' ? distance : -distance);
        strcat(cmd, word);
        moving = true;
    }
    if (!moving)
        return false;
    char word[32];
    snprintf(word, sizeof word, " F%.1f", feed);
    strcat(cmd, word);
    return append(cmd);
}
bool Model::jog_preset(const char *directions, float distance, int speed) {
    if (!directions || strlen(directions) != 3 || speed < 0 || speed > 2)
        return false;
    float rate = 0;
    for (int i = 0; i < 3; i++) {
        if (directions[i] == '0' || state.axis_max_rate[i] <= 0)
            continue;
        if (!rate || state.axis_max_rate[i] < rate)
            rate = state.axis_max_rate[i];
    }
    if (!rate)
        rate = 1000; // Same fallback as Ooznest Control before settings arrive.
    const float factor[] = {1, .5f, .25f};
    return jog_vector(directions, distance, fmaxf(.1f, rate * factor[speed]));
}
bool Model::home(char axis) {
    if (axis && !strchr("XYZA", axis))
        return false;
    if (!state.online || !state.identified || !state.status || state.busy || state.sd_active ||
        (strcmp(state.mode, "Idle") && strncmp(state.mode, "Alarm", 5)))
        return false;
    state.alarm = state.error = 0;
    char cmd[] = {'$', 'H', axis, 0};
    return append(cmd);
}
bool Model::actuator_home(char axis) {
    const char *axes = "XYZA";
    const char *a = axis ? strchr(axes, axis) : nullptr;
    if (!state.extended || !a || unsigned(a - axes) >= state.axis_count)
        return false;
    if (!home(axis))
        return false;
    state.homed_axes &= ~(1u << (a - axes));
    return append("$#"); // Read the controller's HOME report after the homing acknowledgement.
}
bool Model::settings_load() {
    if (!idle() || !state.grblhal) return false;
    settings.clear();
    state.settings_count = state.settings_groups = 0;
    state.settings_ready = false;
    state.settings_result = 0;
    state.settings_loading = true;
    sent_ = now_;
    copy(state.settings_message, "Reading controller settings", sizeof state.settings_message);
    return append("$EG") && append("$ES") && append("$ESH") && append("$$");
}
bool Model::setting_write(int id, const char *value) {
    if (!idle() || !state.grblhal || !state.settings_ready || !value) {
        copy(state.settings_message, "Controller must be connected, idle and settings loaded", sizeof state.settings_message);
        return false;
    }
    const auto *s = settings.find(id);
    std::string error;
    if (!s || !SettingsCatalog::validate(*s, value, error)) {
        snprintf(state.settings_message, sizeof state.settings_message, "%s", s ? error.c_str() : "Setting unavailable");
        return false;
    }
    if (strpbrk(value, "!?~") && (!state.legacy_rt_known || state.legacy_rt)) {
        copy(state.settings_message, "These characters require legacy realtime commands ($39) disabled", sizeof state.settings_message);
        return false;
    }
    if (s->type != 8 && s->value == value) {
        state.settings_result = 1;
        copy(state.settings_message, "Unchanged", sizeof state.settings_message);
        return true;
    }
    setting_write_id_ = id;
    setting_password_ = s->type == 8;
    setting_reboot_ = s->reboot;
    setting_requested_ = value;
    setting_value_seen_ = false;
    state.settings_ready = false;
    state.settings_result = 0;
    state.settings_saving = true;
    sent_ = now_;
    state.settings_count = state.settings_groups = 0;
    copy(state.settings_message, "Saving and checking controller readback", sizeof state.settings_message);
    char command[640];
    snprintf(command, sizeof command, "$%d=%s", id, value);
    settings.clear();
    state.machine_known = state.coordinates = state.wco_known = false;
    state.wco_axes = 0;
    if (id == 376) { state.rotary_known = false; state.homed_axes &= ~8u; }
    return append(command) && append("$EG") && append("$ES") && append("$ESH") && append("$$");
}
bool Model::actuator_type(bool rotary) {
    if (!idle() || !state.extended || state.axis_count < 4 || !state.rotary_known)
        return false;
    if (rotary == bool(state.rotary_axes & 8))
        return true;
    unsigned mask = state.rotary_axes >> 3;
    mask = rotary ? mask | 1u : mask & ~1u;
    char command[32];
    snprintf(command, sizeof command, "$376=%u", mask);
    if (!append(command) || !append("$$"))
        return false;
    state.rotary_known = state.machine_known = state.coordinates = state.wco_known = false;
    state.wco_axes = 0;
    state.homed_axes &= ~8u;
    return true;
}
bool Model::actuator_move(int axis, float target, float feed, float end1, float end2, bool rotary) {
    if (!idle() || actuator_pending_ || state.cycle_axis >= 0 || !state.extended || !state.machine_known || (state.spindle != 0 || state.spindle_direction != 0) || axis < 0 || axis >= 4 ||
        unsigned(axis) >= state.axis_count || !(state.homed_axes & (1u << axis)) ||
        !isfinite(target) || !isfinite(feed) || !isfinite(end1) || !isfinite(end2) ||
        end1 == end2 || fabsf(end1) > 100000 || fabsf(end2) > 100000 ||
        target < fminf(end1, end2) || target > fmaxf(end1, end2) || feed < .1f || feed > 1000000 ||
        (rotary && axis != 3) || (axis == 3 && (!state.rotary_known || rotary != bool(state.rotary_axes & 8))))
        return false;
    if (state.axis_max_rate[axis] > 0)
        feed = fminf(feed, state.axis_max_rate[axis]);
    char cmd[100];
    snprintf(cmd, sizeof cmd, "$J=G53 G90 G21 %c%.3f F%.1f", "XYZA"[axis], target, feed);
    if (!append(cmd))
        return false;
    actuator_pending_ = true;
    copy(state.mode, "Jog", sizeof state.mode);
    return true;
}
bool Model::actuator_cycle(int axis, float feed, float end1, float end2, bool rotary) {
    if (state.cycle_axis >= 0 || axis < 0 || axis >= 4 ||
        state.machine[axis] < fminf(end1, end2) - .01f || state.machine[axis] > fmaxf(end1, end2) + .01f)
        return false;
    float target = fabsf(state.machine[axis] - end2) <= .01f ? end1 : end2;
    if (!actuator_move(axis, target, feed, end1, end2, rotary))
        return false;
    cycle_ends_[0] = end1;
    cycle_ends_[1] = end2;
    cycle_target_ = target;
    cycle_feed_ = feed;
    cycle_rotary_ = rotary;
    cycle_resume_ = false;
    cycle_speed_pending_ = false;
    state.cycle_axis = axis;
    return true;
}
bool Model::actuator_cycle_speed(float feed) {
    int axis = state.cycle_axis;
    if (axis < 0 || !state.online || !state.status || strncmp(state.mode, "Jog", 3) ||
        !isfinite(feed) || feed < .1f || feed > 1000000)
        return false;
    if (state.busy) {
        cycle_feed_ = feed;
        cycle_speed_pending_ = true;
        return true;
    }
    if (!jog_cancel())
        return false;
    // Restart the same leg only after cancellation has produced fresh Idle feedback.
    cycle_feed_ = feed;
    state.cycle_axis = axis;
    cycle_resume_ = true;
    return true;
}
bool Model::unlock() {
    if (!state.online || !state.identified || state.busy || strncmp(state.mode, "Alarm", 5))
        return false;
    state.alarm = state.error = 0;
    return append("$X");
}
bool Model::pause() {
    if (!state.status || (!state.local_job && strncmp(state.mode, "Run", 3)))
        return false;
    if(state.local_job)stream_paused_ = true;
    return rt(state.grblhal ? 0x82 : '!');
}
bool Model::resume() {
    if (!state.status || strncmp(state.mode, "Hold:0", 6))
        return false;
    stream_paused_ = false;
    return rt(state.grblhal ? 0x81 : '~');
}
bool Model::stop() {
    if (!realtime_(context_, 0x18))
        return false;
    cancel("Reset sent. Wait for controller identification.");
    state.macro_failed = false;
    state.status = false;
    state.homed_axes = 0;
    state.machine_known = false;
    state.sd_active = false;
    state.job_complete = state.job_end = false;
    state.download_ready = false;
    return true;
}
bool Model::override_target(bool spindle, int percent) {
    if (!state.online || !state.identified || !state.status || !state.sd_active || !state.overrides_known ||
        state.alarm || state.error || percent < 10 || percent > 200)
        return false;
    int delta = percent - (spindle ? state.spindle_override : state.feed_override);
    if (delta < -200 || delta > 200)
        return false;
    const uint8_t coarse = spindle ? (delta > 0 ? 0x9A : 0x9B) : (delta > 0 ? 0x91 : 0x92);
    const uint8_t fine = spindle ? (delta > 0 ? 0x9C : 0x9D) : (delta > 0 ? 0x93 : 0x94);
    int distance = abs(delta);
    if (distance)
        state.overrides_known = false;
    for (int i = 0; i < distance / 10; i++)
        if (!rt(coarse))
            return false;
    for (int i = 0; i < distance % 10; i++)
        if (!rt(fine))
            return false;
    return true;
}
bool Model::jog_cancel() {
    // Drop an unsent jog before sending cancel, so release cannot start a later move.
    cancel("Jog released");
    copy(state.mode, "Jog", sizeof state.mode);
    return realtime_(context_, 0x85);
}
bool Model::acknowledge_error() {
    if (state.alarm || strncmp(state.mode, "Idle", 4) || state.busy)
        return false;
    state.error = 0;
    copy(state.message, "Error acknowledged. Check setup before continuing.", sizeof state.message);
    return true;
}
bool Model::contact_test(uint32_t now) {
    if (!idle() || !state.probe_known)
        return false;
    if (contact_stage_) return true;
    state.contact_testing = true;
    contact_stage_ = 1;
    contact_started_ = now;
    state.contact_ready = false;
    copy(state.message, "Touch the tool to the plate, then release", sizeof state.message);
    return true;
}
bool Model::probe_corner(const ProbeSettings &p) {
    if (!idle() || !state.contact_ready || state.probe || !state.modal_known || !state.coordinates ||
        !state.wco_known || p.corner < 0 || p.corner > 3)
        return false;
    const float values[] = {p.x, p.y, p.z, p.diameter, p.feed, p.inset};
    for (float v : values)
        if (!isfinite(v) || v <= 0)
            return false;
    if (p.x > 50 || p.y > 50 || p.z > 50 || p.diameter > 50 || p.feed < 25 || p.feed > 500 || p.inset > 100 ||
        (state.spindle > 0 || state.spindle_direction != 0))
        return false;
    // Geometry and ordering follow Ooznest Control's runAutoCorner/stepAutoCorner.
    float xd = (p.corner == 1 || p.corner == 3) ? 1 : -1, yd = p.corner >= 2 ? 1 : -1;
    float xo = xd * (p.diameter / 2 + p.x), yo = yd * (p.diameter / 2 + p.y);
    char cmd[128];
    auto add = [&](const char *fmt, float a = 0, float b = 0) {
        snprintf(cmd, sizeof cmd, fmt, a, b);
        return append(cmd);
    };
    append("G21 G90");
    append("G10 P0 L20 X0 Y0 Z0");
    add("G0 X%.3f Y%.3f", -xd * p.inset, -yd * p.inset);
    add("G38.2 Z-25 F%.1f", p.feed);
    append("G4 P0.4");
    add("G10 P0 L20 Z%.3f", p.z);
    add("G0 Z%.3f", p.z + 5);
    add("G0 X%.3f Y%.3f", xd * 20, -yd * 10);
    add("G0 Z%.3f", p.z - 6);
    add("G38.2 X%.3f F%.1f", -xd * 25, p.feed);
    append("G4 P0.4");
    add("G10 P0 L20 X%.3f", xo);
    add("G0 X%.3f", xo + xd * 2);
    add("G0 Z%.3f", p.z + 15);
    add("G0 Y%.3f", yd * 20);
    add("G0 X%.3f", -xd * 15);
    add("G0 Z%.3f", p.z - 6);
    add("G38.2 Y%.3f F%.1f", -yd * 25, p.feed);
    append("G4 P0.4");
    add("G10 P0 L20 Y%.3f", yo);
    add("G0 Y%.3f", yo + yd * 2);
    add("G0 Z%.3f", p.z + 5);
    append("G0 X0 Y0");
    append("G21");
    append(state.absolute ? "G90" : "G91");
    append("$G");
    state.probing = true;
    state.probe_stage = 0;
    state.probe_axis = -1;
    state.contact_ready = false;
    copy(state.message, "Corner probe running; work offsets will change", sizeof state.message);
    return true;
}
bool Model::probe_surface(const ProbeSettings &p, int axis, int direction) {
    if (!idle() || !state.contact_ready || state.probe || !state.modal_known || !state.coordinates ||
        !state.wco_known || axis < 0 || axis > 2 || (direction != -1 && direction != 1) ||
        (axis == 2 && direction != -1) || state.spindle > 0 || state.spindle_direction != 0)
        return false;
    float plate = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
    if (!isfinite(plate) || plate <= 0 || plate > 50 || !isfinite(p.feed) || p.feed < 25 || p.feed > 500 ||
        (axis != 2 && (!isfinite(p.diameter) || p.diameter <= 0 || p.diameter > 50)))
        return false;
    // Ooznest runSingleAxis: relative probe, selected-axis offset, opposite retract.
    // Z uses plate thickness only; tool radius belongs to X/Y edges.
    char axis_name = "XYZ"[axis], command[96];
    float offset = axis == 2 ? plate : -direction * (plate + p.diameter / 2);
    append("G21 G91");
    snprintf(command, sizeof command, "G38.2 %c%.3f F%.1f", axis_name, direction * 25.f, p.feed);
    append(command);
    snprintf(command, sizeof command, "G10 L20 P0 %c%.3f", axis_name, offset);
    append(command);
    snprintf(command, sizeof command, "G0 %c%.3f", axis_name, -direction * 2.f);
    append(command);
    append(state.absolute ? "G90" : "G91");
    append("$G");
    append("$#");
    state.probing = true;
    state.probe_stage = 0;
    state.probe_axis = axis;
    state.contact_ready = false;
    copy(state.message, "Surface probe running; selected work offset will change", sizeof state.message);
    return true;
}
void Model::status_report(const char *line) {
    char data[768];
    copy(data, line + 1, sizeof data);
    char *end = strchr(data, '>');
    if (!end)
        return;
    *end = 0;
    char *save = nullptr, *token = strtok_r(data, "|", &save);
    if (!token)
        return;
    copy(state.mode, token, sizeof state.mode);
    if (!strncmp(state.mode, "Alarm:", 6))
        state.alarm = atoi(state.mode + 6);
    else if (!strncmp(state.mode, "Alarm", 5) && !state.alarm)
        state.alarm = -1;
    state.status = true;
    state.last_status = now_;
    if (actuator_pending_ && !state.busy && !strcmp(state.mode, "Idle"))
        actuator_pending_ = false;
    if (!strncmp(state.mode, "Alarm", 5))
        state.homed_axes = 0;
    float coords[4] = {};
    unsigned coord_axes = 0;
    bool machine = false, position = false, sd = false, pins = false;
    constexpr uint32_t probes = (1u << ('I'-'A')) | (1u << ('J'-'A')) | (1u << ('K'-'A'));
    // Limits/control inputs are snapshots. Only the acquired I/J/K probes are deltas.
    state.inputs &= probes;
    if (state.pins_reporting) {
        for (const char *p = "XYZABCUVWRHSDLTEOFMQP"; *p; ++p) state.inputs_known |= 1u << (*p-'A');
    }
    if (full_report_) {
        state.probe = false;
        state.probe_known = true;
        full_report_ = false;
    }
    while ((token = strtok_r(nullptr, "|", &save))) {
        if (!strncmp(token, "WCO:", 4)) {
            float v[4];
            if ((state.wco_axes = vector4(token + 4, v))) {
                for (unsigned i = 0; i < state.wco_axes; i++)
                    state.wco[i] = v[i] * (state.report_inches && !(state.rotary_axes & (1u << i)) ? 25.4f : 1);
                state.wco_known = true;
            }
        } else if (!strncmp(token, "MPos:", 5) || !strncmp(token, "WPos:", 5)) {
            if ((coord_axes = vector4(token + 5, coords))) {
                machine = token[0] == 'M';
                position = true;
            }
        } else if (!strncmp(token, "H:", 2)) {
            const char *comma = strchr(token + 2, ',');
            // A lone H:1 means the configured homing cycle, not necessarily every axis.
            if (comma) {
                char *end;
                unsigned long mask = strtoul(comma + 1, &end, 10);
                if (end > comma + 1 && !*end && mask <= 255)
                    state.homed_axes = mask & 15;
            }
            else if (token[2] == '0')
                state.homed_axes = 0;
        } else if (!strncmp(token, "FS:", 3)) {
            float f, s;
            if (sscanf(token + 3, "%f,%f", &f, &s) == 2 && isfinite(f) && isfinite(s)) {
                state.feed = f;
                state.spindle = s;
            }
        } else if (!strncmp(token, "F:", 2)) {
            float f = strtof(token + 2, nullptr);
            if (isfinite(f))
                state.feed = f;
        } else if (!strncmp(token, "Ov:", 3)) {
            int feed, rapid, spindle;
            if (sscanf(token + 3, "%d,%d,%d", &feed, &rapid, &spindle) == 3 && feed >= 1 && feed <= 65535 &&
                spindle >= 1 && spindle <= 65535) {
                state.feed_override = feed;
                state.spindle_override = spindle;
                state.overrides_known = true;
            }
        } else if (!strncmp(token, "Ln:", 3))
            state.line = atoi(token + 3);
        else if (!strncmp(token, "Alarm:", 6))
            state.alarm = atoi(token + 6);
        else if (!strncmp(token, "Pn:", 3)) {
            pins = true;
            const char *p = token + 3;
            const char *normal = "XYZABCUVWRHSDLTEOFMQP";
            for (const char *n = normal; *n; ++n) state.inputs_known |= 1u << (*n-'A');
            for (const char *n = p; *n; ++n) {
                char upper = toupper((unsigned char)*n);
                if (upper < 'A' || upper > 'Z') continue;
                uint32_t bit = 1u << (upper-'A');
                if (*n >= 'A' && *n <= 'Z') state.inputs |= bit;
                else if (bit & probes) state.inputs &= ~bit;
                state.inputs_known |= bit;
            }
            if (state.extended) {
                if (strchr(p, 'I')) {
                    state.probe = true;
                    state.probe_known = true;
                } else if (strchr(p, 'i')) {
                    state.probe = false;
                    state.probe_known = true;
                }
            } else {
                state.probe = strchr(p, 'P') != nullptr;
                state.probe_known = true;
            }
        } else if (!strncmp(token, "A:", 2)) {
            state.spindle_direction = strchr(token + 2, 'C') ? -1 : strchr(token + 2, 'S') ? 1 : 0;
            state.coolant = (strchr(token + 2, 'M') ? 1u : 0u) | (strchr(token + 2, 'F') ? 2u : 0u);
            state.accessories_known = true;
        } else if (!strncmp(token, "SD:", 3)) {
            char *comma = strchr(token + 3, ',');
            if (comma) {
                *comma = 0;
                float pct = strtof(token + 3, nullptr);
                if (isfinite(pct) && pct >= 0 && pct <= 100) {
                    state.percent = pct;
                    copy(state.filename, comma + 1, sizeof state.filename);
                    sd = true;
                }
            }
        }
    }
    if (state.alarm)
        state.homed_axes = 0;
    if (!state.extended && !pins) {
        state.probe = false;
        state.probe_known = true;
    }
    if (position) {
        state.axis_count = coord_axes;
        state.machine_known = machine || (state.wco_known && state.wco_axes >= coord_axes);
        for (unsigned i = 0; i < coord_axes; i++) {
            float v = coords[i] * (state.report_inches && !(state.rotary_axes & (1u << i)) ? 25.4f : 1);
            if (machine)
                state.machine[i] = v;
            else
                state.work[i] = v;
            if (state.wco_known && i < state.wco_axes) {
                if (machine)
                    state.work[i] = v - state.wco[i];
                else
                    state.machine[i] = v + state.wco[i];
            }
        }
        state.coordinates = true;
    }
    if (sd) {
        state.job_complete = false;
        if (!state.sd_active) {
            state.job_started = now_;
            state.elapsed = 0;
            state.job_seen_run = state.job_end = state.job_failed = false;
        }
        state.sd_active = true;
    }
    if (state.sd_active && !strncmp(state.mode, "Run", 3))
        state.job_seen_run = true;
    if (state.sd_active) {
        state.elapsed = (now_ - state.job_started) / 1000;
        if (!state.local_job && !sd && !strcmp(state.mode, "Idle") && (state.job_seen_run || state.job_end) && !state.alarm &&
            !state.error && !state.job_failed) {
            state.sd_active = false;
            state.job_complete = true;
            state.percent = 100;
            copy(state.message, "SD job complete", sizeof state.message);
        }
    }
    if (!strncmp(state.mode, "Alarm", 5) || !strncmp(state.mode, "Door", 4))
        cancel("Machine interlock active. Check controller before recovery.");
    if (state.local_job && stream_.done() && !strcmp(state.mode,"Idle") && !state.job_failed && !state.alarm && !state.error) {
        stream_.cancel(); state.local_job = state.job_preparing = state.sd_active = state.busy = false;
        state.job_complete = true; state.percent = 100; state.modal_known = false;
        append("$G"); append("$#");
        copy(state.message,"Job complete",sizeof state.message);
    }
    if (goto_pending_ && cursor_ >= queued_ && !awaiting_ && !strcmp(state.mode, "Idle"))
        goto_pending_ = false;
    if (state.macro_running && cursor_ >= queued_ && !awaiting_ && !strcmp(state.mode, "Idle"))
        state.macro_running = false;
    if (state.cycle_axis >= 0) {
        int axis = state.cycle_axis;
        bool valid = state.machine_known && unsigned(axis) < state.axis_count &&
                     (state.homed_axes & (1u << axis)) && !state.alarm && !state.error &&
                     state.spindle == 0 && state.spindle_direction == 0 && !state.sd_active && !state.uploading &&
                     (axis != 3 || (state.rotary_known && cycle_rotary_ == bool(state.rotary_axes & 8)));
        if (!valid || (strcmp(state.mode, "Idle") && strncmp(state.mode, "Jog", 3))) {
            char mode[sizeof state.mode];
            copy(mode, state.mode, sizeof mode);
            jog_cancel();
            copy(state.mode, mode, sizeof state.mode);
        } else if (position && !strcmp(state.mode, "Idle") && !state.busy && !actuator_pending_) {
            bool reached = fabsf(state.machine[axis] - cycle_target_) <= .01f;
            if (!cycle_resume_ && !reached) {
                state.cycle_axis = -1;
                copy(state.message, "Repeat stopped before its endpoint", sizeof state.message);
            } else {
                if (reached)
                    cycle_target_ = cycle_target_ == cycle_ends_[1] ? cycle_ends_[0] : cycle_ends_[1];
                cycle_resume_ = false;
                cycle_speed_pending_ = false;
                state.cycle_axis = -1;
                if (actuator_move(axis, cycle_target_, cycle_feed_, cycle_ends_[0], cycle_ends_[1], cycle_rotary_))
                    state.cycle_axis = axis;
            }
        }
    }
}
void Model::receive(const char *line, uint32_t now) {
    now_ = now;
    if (!line || !*line)
        return;
    if (!state.online)
        return;
    if (!strncmp(line, "[NEWOPT:", 8)) {
        state.ymodem = false;
        const char *p = line + 8;
        while (*p && *p != ']') {
            size_t n = strcspn(p, ",]");
            if (n == 2 && !strncmp(p, "YM", 2))
                state.ymodem = true;
            p += n;
            if (*p == ',')
                p++;
            else
                break;
        }
    }
    if (!strncmp(line, "Grbl", 4)) {
        begin(now);
        return;
    }
    if (line[0] == '<' && strlen(line) < 767) {
        status_report(line);
        return;
    }
    if (state.downloading && awaiting_ && strncmp(line, "error:", 6) && strncmp(line, "ALARM:", 6)) {
        size_t n = strlen(line);
        bool complete = n >= 2 && !strcmp(line + n - 2, "ok");
        if (complete)
            n -= 2; // Older controllers can append the acknowledgement to the final line.
        if (n) {
            if (state.download_bytes + n + 1 > MaxPreviewBytes ||
                (!download_failed_ && (!download_sink_(download_context_, line, n) ||
                                       !download_sink_(download_context_, "\n", 1))))
                download_failed_ = true;
            state.download_bytes += n + 1;
        }
        sent_ = now;
        if (complete) {
            state.downloading = false;
            state.status = false;
            identified_at_ = now;
            state.download_ready = !download_failed_ && state.download_bytes > 0;
            state.download_revision++;
            copy(state.message,
                 state.download_ready ? "Preview downloaded" : "Preview failed or empty; no toolpath loaded",
                 sizeof state.message);
            awaiting_ = false;
            cursor_++;
        }
        return;
    }
    if (state.spindles_loading && !strncmp(line, "[SPINDLE:", 9)) {
        char data[256]; copy(data, line + 9, sizeof data);
        char *end = strchr(data, ']');
        if (!end || end[1]) return;
        *end = 0;
        char *fields[6], *p = data;
        unsigned count = 0;
        do { fields[count++] = p; p = strchr(p, '|'); if (p) *p++ = 0; } while (p && count < 6);
        if (count < 5 || state.spindle_count >= 8) return;
        Spindle s;
        if (strcmp(fields[1], "-")) {
            char *tail; long n = strtol(fields[1], &tail, 10);
            if (!*fields[1] || *tail || n < 0 || n > 255) return;
            s.number = int(n);
        }
        copy(s.name, fields[4], sizeof s.name);
        for (char *c = s.name; *c; ++c) if ((unsigned char)*c < 32) *c = ' ';
        s.active = strchr(fields[3], '*') != nullptr;
        if (count == 6) {
            char tail;
            s.limits_known = sscanf(fields[5], "%f,%f%c", &s.minimum, &s.maximum, &tail) == 2 &&
                isfinite(s.minimum) && isfinite(s.maximum) && s.minimum >= 0 && s.maximum > s.minimum;
        }
        state.spindles[state.spindle_count++] = s;
        return;
    }
    if (!strncmp(line, "[VER:", 5)) {
        const char *p = line;
        while (*p) {
            if (*p == '.' && strlen(p + 1) >= 8) {
                char build[9];
                memcpy(build, p + 1, 8);
                build[8] = 0;
                bool digits = true;
                for (char c : build)
                    if (c && !isdigit((unsigned char)c))
                        digits = false;
                if (digits)
                    state.build = strtoul(build, nullptr, 10);
            }
            p++;
        }
    }
    if (!strcmp(line, "[FIRMWARE:grblHAL]")) {
        state.grblhal = true;
        if (!state.identified) {
            state.identified = true;
            state.extended = state.build > 20261013;
            identified_at_ = now;
            if (state.extended) {
                full_report_ = true;
                rt(0x87);
            }
            copy(state.message, "grblHAL identified", sizeof state.message);
        }
        return;
    }
    if (settings.receive(line)) {
        state.settings_count = int(settings.entries.size());
        state.settings_groups = int(settings.groups.size());
        if (state.settings_loading || state.settings_saving) sent_ = now;
        if (state.settings_saving && awaiting_ && cursor_ < queued_ && queue_[cursor_] == "$$") {
            char prefix[24]; snprintf(prefix, sizeof prefix, "$%d=", setting_write_id_);
            if (!strncmp(line, prefix, strlen(prefix))) setting_value_seen_ = true;
        }
    }
    if (!strncmp(line, "$39=", 4) && (!strcmp(line + 4, "0") || !strcmp(line + 4, "1"))) {
        state.legacy_rt_known = true;
        state.legacy_rt = line[4] == '1';
    }
    if (!strncmp(line, "[GC:", 4)) {
        state.modal_known = true;
        state.inches = strstr(line, "G20 ") != nullptr;
        state.absolute = strstr(line, "G91 ") == nullptr;
        for (int w = 54; w <= 59; w++) {
            char word[8];
            snprintf(word, sizeof word, "G%d ", w);
            if (strstr(line, word))
                state.wcs = w;
        }
    }
    if (!strncmp(line, "$30=", 4) || !strncmp(line, "$31=", 4)) {
        char *end;
        float value = strtof(line + 4, &end);
        if (end != line + 4 && !*end && isfinite(value) && value >= 0) {
            if (line[2] == '0') state.spindle_max = value;
            else state.spindle_min = value;
            state.spindle_limits_seen |= line[2] == '0' ? 1 : 2;
            state.spindle_limits_known = state.spindle_limits_seen == 3 && state.spindle_max > state.spindle_min;
        }
    }
    if (!strncmp(line, "$10=", 4)) {
        char *end;
        unsigned long mask = strtoul(line + 4, &end, 10);
        if (end != line + 4 && !*end && mask <= 65535) {
            state.pins_reporting_known = true;
            state.pins_reporting = mask & 16;
            if (!state.pins_reporting) state.inputs_known = 0;
        }
    }
    if (!strncmp(line, "$376=", 5)) {
        char *end;
        long mask = strtol(line + 5, &end, 10);
        if (end != line + 5 && !*end && mask >= 0 && mask <= 31) {
            uint8_t axes = uint8_t(mask << 3);
            if (axes != state.rotary_axes || !state.rotary_known) {
                state.homed_axes &= ~(axes ^ state.rotary_axes);
                state.machine_known = state.coordinates = state.wco_known = false;
                state.wco_axes = 0; // Re-read positions with the newly confirmed units.
            }
            state.rotary_axes = axes;
            state.rotary_known = true;
        }
    }
    if (!strncmp(line, "$13=", 4)) {
        bool inches = atoi(line + 4) != 0;
        if (state.settings_saving && state.report_units_known && inches != state.report_inches) {
            // A status received before readback may already use the new units.
            state.machine_known = state.coordinates = state.wco_known = false;
            state.wco_axes = 0;
        }
        // Only the first $13 report reinterprets positions received before units were known.
        if (!state.report_units_known && inches != state.report_inches) {
            float f = inches ? 25.4f : 1 / 25.4f;
            for (int i = 0; i < 4; i++) {
                if (state.rotary_axes & (1u << i))
                    continue;
                state.machine[i] *= f;
                state.work[i] *= f;
                state.wco[i] *= f;
            }
        }
        state.report_inches = inches;
        state.report_units_known = true;
    }
    for (int i = 0; i < 4; i++) {
        char setting[8];
        snprintf(setting, sizeof setting, "$%d=", 110 + i);
        if (!strncmp(line, setting, strlen(setting))) {
            char *end;
            float rate = strtof(line + strlen(setting), &end);
            if (*end == 0 && isfinite(rate) && rate > 0 && rate <= 1000000)
                state.axis_max_rate[i] = rate;
        }
    }
    if (!strncmp(line, "[FILE:", 6) || !strncmp(line, "[DIR:", 5)) {
        if (!state.listing)
            return;
        const char *p = line + (line[1] == 'F' ? 6 : 5);
        const char *e = strpbrk(p, "|]");
        if (!e || e == p)
            return;
        size_t n = e - p;
        bool slash = p[0] != '/';
        if (n + slash >= sizeof state.files[0].path || state.file_count >= MaxFiles) {
            copy(state.message, "SD list limit reached; narrow the directory on the controller",
                 sizeof state.message);
            return;
        }
        File &f = state.files[state.file_count++];
        memset(&f, 0, sizeof f);
        if (slash)
            f.path[0] = '/';
        memcpy(f.path + slash, p, n);
        f.path[n + slash] = 0;
        const char *size = strstr(line, "SIZE:");
        long bytes = size ? strtol(size + 5, nullptr, 10) : 0;
        f.directory = line[1] == 'D' || bytes < 0;
        f.unusable = strstr(line, "|UNUSABLE") != nullptr || !valid_sd_path(f.path);
        f.size = bytes > 0 ? bytes : 0;
        state.file_revision++;
    }
    if (!strncmp(line, "ALARM:", 6) || !strncmp(line, "error:", 6)) {
        if (state.local_job) rt(state.grblhal ? 0x82 : '!');
        if (state.spindles_loading && awaiting_ && cursor_ < queued_ && queue_[cursor_] == "$SPINDLESH" && line[0] == 'e') {
            cancel("Spindle discovery unavailable");
            return;
        }
        if (state.macro_running && line[0] != 'A') rt(state.grblhal ? 0x82 : '!');
        if (state.sd_active)
            state.job_failed = true;
        bool alarm = line[0] == 'A';
        int code = atoi(line + 6);
        if (alarm) {
            state.homed_axes = 0;
            state.alarm = code;
            copy(state.mode, "Alarm", sizeof state.mode);
        } else
            state.error = code;
        char message[192];
        snprintf(message, sizeof message, "%s%s%s", fault_text(alarm, code), cursor_ < queued_ ? " / " : "",
                 setting_password_ && state.settings_saving ? "Password setting" : cursor_ < queued_ ? queue_[cursor_].c_str() : "");
        cancel(message);
        return;
    }
    if (!strncmp(line, "[HOME:", 6)) {
        const char *colon = strrchr(line, ':');
        if (colon && colon != line + 5) {
            char *end;
            unsigned long mask = strtoul(colon + 1, &end, 10);
            if (end > colon + 1 && !strcmp(end, "]") && mask <= 15)
                state.homed_axes = mask;
        }
        return;
    }
    if (!strncmp(line, "[MSG:", 5)) {
        copy(state.message, line, sizeof state.message);
        if (strstr(line, "Program End") && state.sd_active) {
            state.job_end = true;
        }
        return;
    }
    if (!strncmp(line, "[PRB:", 5) && state.probing) {
        const char *colon = strrchr(line, ':');
        if (!colon || atoi(colon + 1) != 1) {
            state.error = 4;
            cancel("Probe did not trigger; offsets may have changed. Check setup.");
            return;
        }
        got_probe_ = true;
        state.probe_stage++;
    }
    if (state.local_job && !strcmp(line,"ok")) { stream_.acknowledge(now); return; }
    if (!strcmp(line, "ok") && awaiting_) {
        if (state.spindles_loading && queue_[cursor_] == "$SPINDLESH") {
            state.spindles_loading = false;
            state.spindles_result = state.spindle_count ? 1 : -1;
            state.spindle_revision++;
            for (unsigned i = 0; i < state.spindle_count; ++i) if (state.spindles[i].active) {
                const auto &s = state.spindles[i];
                state.spindle_limits_known = s.limits_known;
                state.spindle_min = s.minimum; state.spindle_max = s.maximum;
            }
        }
        awaiting_ = false;
        if (!need_probe_ || got_probe_) {
            cursor_++;
            need_probe_ = got_probe_ = false;
        }
    }
}
bool Model::run_local(FILE *file,const char *path,uint32_t size,uint32_t now) {
    if (!idle() || !file || !size || !stream_.start(file,size,now)) return false;
    state.local_job = state.job_preparing = state.sd_active = state.busy = true;
    state.job_complete = state.job_failed = state.job_seen_run = state.job_end = false;
    stream_paused_ = false;
    state.percent = 0; state.job_bytes = 0; state.job_total = size; state.job_started = now;
    copy(state.filename,path,sizeof state.filename);
    return true;
}
bool Model::spindle_list() {
    if (!idle() || !state.grblhal) return false;
    state.spindle_count = 0; state.spindles_result = 0;
    state.spindles_loading = true;
    state.spindle_revision++;
    return append("$SPINDLESH");
}
bool Model::spindle_select(int number) {
    if (!idle() || !state.grblhal || !state.accessories_known || state.spindle_direction || state.spindle > 0) return false;
    bool found = false;
    for (unsigned i = 0; i < state.spindle_count; ++i)
        if (state.spindles[i].number == number && number >= 0) found = true;
    if (!found) return false;
    char command[32]; snprintf(command, sizeof command, "M104Q%d", number);
    state.spindle_limits_known = false;
    state.spindle_count = 0; state.spindles_result = 0; state.spindles_loading = true;
    state.spindle_revision++;
    return append(command) && append("$SPINDLESH");
}
bool Model::spindle_control(int direction, float rpm) {
    if (!idle() || direction < -1 || direction > 1 || !isfinite(rpm)) return false;
    if (direction && (!state.spindle_limits_known || rpm <= 0 || rpm < state.spindle_min || rpm > state.spindle_max)) return false;
    char command[64];
    if (direction) snprintf(command, sizeof command, "M%d S%.0f", direction == 1 ? 3 : 4, rpm);
    else snprintf(command, sizeof command, "M5");
    return append(command);
}
bool Model::coolant_control(unsigned mode) {
    if (!idle() || mode > 3) return false;
    append("M9");
    if (mode & 1) append("M7");
    if (mode & 2) append("M8");
    return true;
}
bool Model::macro_run(const char *code) {
    std::vector<std::string> lines;
    if (!idle() || !macro_lines(code, lines)) return false;
    for (const auto &s : lines) append(s.c_str());
    append("$G"); // A macro may change WCS, units or distance mode used by manual controls.
    state.modal_known = false;
    state.macro_running = true;
    return true;
}
void Model::tick(uint32_t now) {
    now_ = now;
    bool homing = awaiting_ && cursor_ < queued_ && !strncmp(queue_[cursor_].c_str(), "$H", 2);
    if (!state.online)
        return;
    if (!state.identified) {
        if (now - sent_ > 5000) {
            disconnect();
            copy(state.message, "No grblHAL identification. Check UART pins, baud and controller firmware.",
                 sizeof state.message);
        }
        return;
    }
    if (now - poll_ >= 250) {
        rt(state.extended ? 0x80 : '?');
        poll_ = now;
    }
    if (!initialized_ && now - identified_at_ >= 250) {
        initialized_ = true;
        append("$G");
        append("$#");
        append("$$");
    }
    bool settings_io = state.settings_loading || state.settings_saving;
    if ((!state.downloading && !settings_io && ((state.status && now - state.last_status > 2000) ||
                                (!state.status && now - identified_at_ > 5000))) ||
        ((state.downloading || settings_io) && now - sent_ > 10000)) {
        rt(state.probing || homing ? 0x18 : '!');
        cancel("Status timed out. Controls locked; reconnect the controller.");
        state.status = false;
        state.online = false;
        return;
    }
    if (state.local_job) {
        stream_.tick(now, !stream_paused_ && !state.alarm && !state.error && (!strcmp(state.mode,"Idle") || !strcmp(state.mode,"Run")));
        state.job_preparing = stream_.checking();
        state.job_bytes = state.job_preparing ? stream_.checked_bytes() : stream_.acknowledged();
        state.percent = state.job_total ? fminf(99.9f,100.f*state.job_bytes/state.job_total) : 0;
        if (stream_.failed()) {
            char reason[128]; copy(reason,stream_.error(),sizeof reason);
            rt(state.grblhal ? 0x82 : '!'); state.error = 1; state.job_failed = true;
            cancel(reason);
        }
        return;
    }
    if (contact_stage_) {
        if (now - contact_started_ > 15000) {
            contact_stage_ = 0;
            state.contact_testing = false;
            copy(state.message, "Contact test timed out", sizeof state.message);
        } else if (contact_stage_ == 1 && state.probe) {
            contact_stage_ = 2;
            copy(state.message, "Contact detected. Release the tool from the plate.", sizeof state.message);
        } else if (contact_stage_ == 2 && !state.probe) {
            contact_stage_ = 0;
            state.contact_testing = false;
            state.contact_ready = true;
            copy(state.message, "Contact verified. Position above the plate hole.", sizeof state.message);
        }
    }
    if (awaiting_ || need_probe_) {
        if (!awaiting_ && need_probe_ && got_probe_) {
            cursor_++;
            need_probe_ = got_probe_ = false;
        } else {
            if (now - sent_ > (homing || state.macro_running ? 300000u : state.probing ? 120000u : 10000u)) {
                if (state.macro_running) rt(state.grblhal ? 0x82 : '!');
                if (state.probing || homing)
                    rt(0x18);
                if (homing) {
                    state.homed_axes = 0;
                    state.machine_known = false;
                }
                state.error = 1;
                cancel("Command timed out; queue cancelled. Check controller before continuing.");
            }
            return;
        }
    }
    if (cursor_ < queued_) {
        if (line_(context_, queue_[cursor_].c_str())) {
            awaiting_ = true;
            sent_ = now;
            need_probe_ = state.probing && strstr(queue_[cursor_].c_str(), "G38.2") != nullptr;
            got_probe_ = false;
        } else {
            state.error = 1;
            cancel("UART write failed; command queue cancelled");
        }
    } else if (state.busy) {
        if (goto_pending_ || state.macro_running)
            return; // Rapid acknowledgements can precede motion completion.
        bool probed = state.probing;
        for (auto &command : queue_) command.clear();
        queued_ = cursor_ = 0;
        state.busy = state.probing = state.listing = false;
        if (state.settings_loading || state.settings_saving) {
            state.settings_ready = !settings.overflow && !settings.entries.empty();
            bool verified = true;
            if (state.settings_saving) {
                const auto *s = settings.find(setting_write_id_);
                verified = setting_value_seen_ && s && SettingsCatalog::matches(*s, setting_requested_);
            }
            state.settings_result = state.settings_ready && verified ? 1 : -1;
            copy(state.settings_message, !state.settings_ready ? "Settings incomplete; refresh to retry" :
                 !verified ? "Controller readback did not match; check the reported value" :
                 state.settings_saving ? (setting_reboot_ ? "Saved. Restart controller to apply this setting" :
                                         setting_password_ ? "Saved. Controller masks password readback" : "Saved and verified") :
                 "Settings loaded", sizeof state.settings_message);
            state.settings_loading = state.settings_saving = false;
            state.settings_revision++;
            setting_requested_.clear();
            setting_write_id_ = -1;
            if (now - state.last_status > 2000) { state.status = false; identified_at_ = now; }
        }
        if (probed)
            copy(state.message, "Probe complete. Remove the plate before running.",
                 sizeof state.message);
    }
    if (cycle_speed_pending_ && state.cycle_axis >= 0 && !state.busy && !awaiting_ && !strcmp(state.mode, "Jog")) {
        cycle_speed_pending_ = false;
        actuator_cycle_speed(cycle_feed_);
    }
}
} // namespace panel
