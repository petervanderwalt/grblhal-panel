#include "panel_model.h"
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
Model::Model(Line l, Realtime r, void *c) : line_(l), realtime_(r), context_(c) {}
void Model::cancel(const char *message) {
    goto_pending_ = false;
    if (state.downloading)
        state.download_ready = false;
    state.downloading = false;
    queued_ = cursor_ = 0;
    awaiting_ = need_probe_ = got_probe_ = false;
    state.busy = state.probing = state.listing = false;
    state.contact_ready = false;
    contact_stage_ = 0;
    if (!state.job_seen_run)
        state.sd_active = false;
    copy(state.message, message, sizeof state.message);
}
void Model::begin(uint32_t now) {
    state.~State();
    new (&state) State();
    now_ = now;
    poll_ = now;
    identified_at_ = 0;
    initialized_ = false;
    full_report_ = false;
    cancel("Identifying controller with $I");
    state.online = true;
    copy(state.mode, "Connecting", sizeof state.mode);
    if (!line_(context_, "$I"))
        disconnect();
    sent_ = now;
}
void Model::disconnect() {
    cancel("Controller disconnected");
    state.online = false;
    state.status = false;
    state.identified = false;
    state.download_ready = false;
    copy(state.mode, "Disconnected", sizeof state.mode);
}
bool Model::idle() const {
    return state.online && state.identified && state.status && !strcmp(state.mode, "Idle") && !state.alarm &&
           !state.error && !state.busy && !state.sd_active;
}
bool Model::append(const char *s) {
    if (queued_ >= 40 || strlen(s) >= sizeof queue_[0] || strchr(s, '\n') || strchr(s, '\r'))
        return false;
    copy(queue_[queued_++], s, sizeof queue_[0]);
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
    const bool inches = state.inches, absolute = state.absolute;
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
    append(inches ? "G20" : "G21");
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
    if (axis && !strchr("XYZ", axis))
        return false;
    if (!state.online || !state.identified || !state.status || state.busy || state.sd_active ||
        (strcmp(state.mode, "Idle") && strncmp(state.mode, "Alarm", 5)))
        return false;
    state.alarm = state.error = 0;
    char cmd[] = {'$', 'H', axis, 0};
    return append(cmd);
}
bool Model::unlock() {
    if (!state.online || !state.identified || state.busy || strncmp(state.mode, "Alarm", 5))
        return false;
    state.alarm = state.error = 0;
    return append("$X");
}
bool Model::pause() {
    if (!state.status || strncmp(state.mode, "Run", 3))
        return false;
    return rt('!');
}
bool Model::resume() {
    if (!state.status || strncmp(state.mode, "Hold:0", 6))
        return false;
    return rt('~');
}
bool Model::stop() {
    if (!realtime_(context_, 0x18))
        return false;
    cancel("Reset sent. Wait for controller identification.");
    state.status = false;
    state.sd_active = false;
    state.download_ready = false;
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
        state.spindle > 0)
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
    append(state.inches ? "G20" : "G21");
    append(state.absolute ? "G90" : "G91");
    append("$G");
    state.probing = true;
    state.probe_stage = 0;
    state.contact_ready = false;
    copy(state.message, "Corner probe running; work offsets will change", sizeof state.message);
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
    float coords[3];
    bool machine = false, position = false, sd = false, pins = false;
    if (full_report_) {
        state.probe = false;
        state.probe_known = true;
        full_report_ = false;
    }
    while ((token = strtok_r(nullptr, "|", &save))) {
        if (!strncmp(token, "WCO:", 4)) {
            float v[3];
            if (vector3(token + 4, v)) {
                for (int i = 0; i < 3; i++)
                    state.wco[i] = v[i] * (state.report_inches ? 25.4f : 1);
                state.wco_known = true;
            }
        } else if (!strncmp(token, "MPos:", 5) || !strncmp(token, "WPos:", 5)) {
            if (vector3(token + 5, coords)) {
                machine = token[0] == 'M';
                position = true;
            }
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
        } else if (!strncmp(token, "Ln:", 3))
            state.line = atoi(token + 3);
        else if (!strncmp(token, "Alarm:", 6))
            state.alarm = atoi(token + 6);
        else if (!strncmp(token, "Pn:", 3)) {
            pins = true;
            const char *p = token + 3;
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
    if (!state.extended && !pins) {
        state.probe = false;
        state.probe_known = true;
    }
    if (position) {
        for (int i = 0; i < 3; i++) {
            float v = coords[i] * (state.report_inches ? 25.4f : 1);
            if (machine)
                state.machine[i] = v;
            else
                state.work[i] = v;
            if (state.wco_known) {
                if (machine)
                    state.work[i] = v - state.wco[i];
                else
                    state.machine[i] = v + state.wco[i];
            }
        }
        state.coordinates = true;
    }
    if (sd) {
        if (!state.sd_active) {
            state.job_started = now_;
            state.elapsed = 0;
        }
        state.sd_active = true;
    }
    if (state.sd_active && !strncmp(state.mode, "Run", 3))
        state.job_seen_run = true;
    if (state.sd_active) {
        state.elapsed = (now_ - state.job_started) / 1000;
        if (!sd && !strcmp(state.mode, "Idle") && state.job_seen_run) {
            state.sd_active = false;
            state.percent = 100;
            copy(state.message, "SD job complete", sizeof state.message);
        }
    }
    if (!strncmp(state.mode, "Alarm", 5) || !strncmp(state.mode, "Door", 4))
        cancel("Machine interlock active. Check controller before recovery.");
    if (goto_pending_ && cursor_ >= queued_ && !awaiting_ && !strcmp(state.mode, "Idle"))
        goto_pending_ = false;
}
void Model::receive(const char *line, uint32_t now) {
    now_ = now;
    if (!line || !*line)
        return;
    if (!state.online)
        return;
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
    if (!strncmp(line, "$13=", 4)) {
        bool inches = atoi(line + 4) != 0;
        if (inches != state.report_inches) {
            float f = inches ? 25.4f : 1 / 25.4f;
            for (int i = 0; i < 3; i++) {
                state.machine[i] *= f;
                state.work[i] *= f;
                state.wco[i] *= f;
            }
        }
        state.report_inches = inches;
    }
    for (int i = 0; i < 3; i++) {
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
        bool alarm = line[0] == 'A';
        int code = atoi(line + 6);
        if (alarm) {
            state.alarm = code;
            copy(state.mode, "Alarm", sizeof state.mode);
        } else
            state.error = code;
        char message[192];
        snprintf(message, sizeof message, "%s%s%s", fault_text(alarm, code), cursor_ < queued_ ? " / " : "",
                 cursor_ < queued_ ? queue_[cursor_] : "");
        cancel(message);
        return;
    }
    if (!strncmp(line, "[MSG:", 5)) {
        copy(state.message, line, sizeof state.message);
        if (strstr(line, "Program End") && state.sd_active) {
            state.sd_active = false;
            state.percent = 100;
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
    if (!strcmp(line, "ok") && awaiting_) {
        awaiting_ = false;
        if (!need_probe_ || got_probe_) {
            cursor_++;
            need_probe_ = got_probe_ = false;
        }
    }
}
void Model::tick(uint32_t now) {
    now_ = now;
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
    if ((!state.downloading && ((state.status && now - state.last_status > 2000) ||
                                (!state.status && now - identified_at_ > 5000))) ||
        (state.downloading && now - sent_ > 10000)) {
        rt(state.probing ? 0x18 : '!');
        cancel("Status timed out. Controls locked; reconnect the controller.");
        state.status = false;
        state.online = false;
        return;
    }
    if (contact_stage_) {
        if (now - contact_started_ > 15000) {
            contact_stage_ = 0;
            copy(state.message, "Contact test timed out", sizeof state.message);
        } else if (contact_stage_ == 1 && state.probe) {
            contact_stage_ = 2;
            copy(state.message, "Contact detected. Release the tool from the plate.", sizeof state.message);
        } else if (contact_stage_ == 2 && !state.probe) {
            contact_stage_ = 0;
            state.contact_ready = true;
            copy(state.message, "Contact verified. Position above the plate hole.", sizeof state.message);
        }
    }
    if (awaiting_ || need_probe_) {
        if (!awaiting_ && need_probe_ && got_probe_) {
            cursor_++;
            need_probe_ = got_probe_ = false;
        } else {
            if (now - sent_ > (state.probing ? 120000u : 10000u)) {
                if (state.probing)
                    rt(0x18);
                state.error = 1;
                cancel("Command timed out; queue cancelled. Check controller before continuing.");
            }
            return;
        }
    }
    if (cursor_ < queued_) {
        if (line_(context_, queue_[cursor_])) {
            awaiting_ = true;
            sent_ = now;
            need_probe_ = strstr(queue_[cursor_], "G38.2") != nullptr;
            got_probe_ = false;
        } else {
            state.error = 1;
            cancel("UART write failed; command queue cancelled");
        }
    } else if (state.busy) {
        if (goto_pending_)
            return; // Rapid acknowledgements can precede motion completion.
        bool probed = state.probing;
        queued_ = cursor_ = 0;
        state.busy = state.probing = state.listing = false;
        if (probed)
            copy(state.message, "Corner probe complete. Remove the plate before running.",
                 sizeof state.message);
    }
}
} // namespace panel
