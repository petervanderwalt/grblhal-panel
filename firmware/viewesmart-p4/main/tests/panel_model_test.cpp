#include "panel_runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace panel {
namespace {
struct Capture {
    char lines[48][192] = {};
    uint8_t bytes[96] = {};
    int n = 0, b = 0;
    bool fail = false;
    char download[2048] = {};
    size_t downloaded = 0;
    bool fail_download = false;
};
static bool download_data(void *p, const char *data, size_t n) {
    auto &c = *(Capture *)p;
    if (c.fail_download || c.downloaded + n >= sizeof c.download)
        return false;
    memcpy(c.download + c.downloaded, data, n);
    c.downloaded += n;
    c.download[c.downloaded] = 0;
    return true;
}
static bool line(void *p, const char *s) {
    auto &c = *(Capture *)p;
    if (c.n < 48)
        snprintf(c.lines[c.n++], 192, "%s", s);
    return !c.fail;
}
static bool rt(void *p, uint8_t byte) {
    auto &c = *(Capture *)p;
    if (c.b < 96)
        c.bytes[c.b++] = byte;
    return true;
}
static Capture *capture;
static Model *model;
#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            printf("@UGTEST FAIL %s:%d\n", __func__, __LINE__);                                              \
            return false;                                                                                    \
        }                                                                                                    \
    } while (0)
static void ready() {
    capture->n = capture->b = 0;
    capture->fail = false;
    model->begin(0);
    model->receive("[VER:1.1f.20261020:]", 1);
    model->receive("[FIRMWARE:grblHAL]", 2);
    model->receive("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F100 S0]", 3);
    model->receive("<Idle|MPos:110,70,0|WCO:100,50,-5|FS:0,0|Pn:i>", 4);
    model->tick(300);
    for (int i = 0; i < 4; i++) {
        model->receive("ok", 301 + i);
        model->tick(301 + i);
    }
    capture->n = capture->b = 0;
}
static bool identification() {
    capture->n = capture->b = 0;
    model->begin(0);
    CHECK(!strcmp(capture->lines[0], "$I"));
    CHECK(capture->b == 0);
    model->receive("[VER:1.1f.20261020:]", 1);
    model->receive("[FIRMWARE:grblHAL]", 2);
    CHECK(capture->b == 1 && capture->bytes[0] == 0x87);
    model->receive("[FIRMWARE:grblHAL]", 3);
    CHECK(capture->b == 1);
    model->tick(300);
    CHECK(capture->bytes[1] == 0x80);
    capture->b = 0;
    model->begin(0);
    model->receive("[VER:1.1f.20261013:]", 1);
    model->receive("[FIRMWARE:grblHAL]", 2);
    model->tick(300);
    CHECK(capture->b == 1 && capture->bytes[0] == '?');
    model->begin(0);
    model->receive("[VER:1.1f:]", 1);
    model->receive("ok", 2);
    model->tick(5001);
    CHECK(!model->state.online && !model->state.identified);
    return true;
}
static bool coordinates_and_probe_pins() {
    ready();
    CHECK(fabsf(model->state.work[0] - 10) < .001f && model->state.probe_known);
    model->receive("<Idle|WPos:2,3,4|WCO:50,60,70|Pn:I>", 500);
    CHECK(model->state.machine[0] == 52 && model->state.probe);
    model->receive("<Idle|FS:0,0>", 600);
    CHECK(model->state.probe);
    model->receive("<Idle|Pn:i>", 700);
    CHECK(!model->state.probe);
    model->receive("$13=1", 701);
    model->receive("<Idle|MPos:2,3,4|WCO:1,1,1>", 702);
    CHECK(fabsf(model->state.work[0] - 25.4f) < .001f);
    CHECK(model->contact_test(703));
    model->receive("<Idle|Pn:I>", 704);
    model->tick(704);
    model->receive("<Idle|Pn:i>", 705);
    model->tick(705);
    CHECK(model->state.contact_ready);
    return true;
}
static bool sd_and_progress() {
    ready();
    CHECK(model->list());
    model->tick(500);
    CHECK(!strcmp(capture->lines[0], "$F+"));
    model->receive("[FILE:/jobs/test.nc|SIZE:12500]", 501);
    model->receive("[DIR:/jobs]", 502);
    model->receive("ok", 503);
    model->tick(504);
    CHECK(model->state.file_count == 2 && model->state.files[0].size == 12500);
    CHECK(!model->run("/missing.nc"));
    CHECK(!model->run("/jobs"));
    CHECK(model->run("/jobs/test.nc"));
    model->tick(505);
    CHECK(!strcmp(capture->lines[1], "$F=/jobs/test.nc"));
    model->receive("ok", 506);
    model->tick(507);
    model->receive("<Run|SD:37.5,/jobs/test.nc|Ln:125|FS:800,12000>", 600);
    CHECK(model->state.sd_active && model->state.percent == 37.5f && model->state.line == 125);
    CHECK(model->pause() && capture->bytes[capture->b - 1] == (model->state.grblhal ? 0x82 : '!'));
    model->receive("<Hold:1>", 601);
    CHECK(!model->resume());
    model->receive("<Hold:0>", 602);
    CHECK(model->resume() && capture->bytes[capture->b - 1] == (model->state.grblhal ? 0x81 : '~'));
    model->receive("<Idle>", 603);
    CHECK(!model->state.sd_active && model->state.percent == 100);
    return true;
}
static bool commands_and_faults() {
    ready();
    CHECK(model->jog_vector("-+0", 10, 600));
    model->tick(500);
    CHECK(!strcmp(capture->lines[0], "$J=G91 G21 X-10.000 Y10.000 F600.0"));
    ready();
    CHECK(model->jog_vector("00+", 10000, 600));
    CHECK(model->jog_cancel());
    model->tick(500);
    CHECK(capture->n == 0);
    CHECK(!model->jog_vector("0+0", 1, 600));
    model->receive("<Idle>", 501);
    CHECK(model->home('X'));
    model->tick(502);
    CHECK(!strcmp(capture->lines[0], "$HX"));
    ready();
    CHECK(!model->jog('Q', 1, 600));
    CHECK(!model->jog('X', NAN, 600));
    CHECK(model->jog('X', -1, 600));
    model->tick(500);
    CHECK(!strcmp(capture->lines[0], "$J=G91 G21 X-1.000 F600.0"));
    CHECK(model->jog_cancel() && capture->bytes[capture->b - 1] == 0x85);
    model->receive("error:15", 501);
    CHECK(model->state.error == 15 && !model->state.busy);
    model->receive("ok", 502);
    CHECK(model->state.error == 15);
    CHECK(!model->zero(7));
    model->receive("<Idle>", 502);
    CHECK(model->acknowledge_error());
    CHECK(model->zero(7));
    model->tick(503);
    CHECK(!strcmp(capture->lines[1], "G10 L20 P0 X0 Y0 Z0"));
    model->receive("ALARM:2", 504);
    CHECK(model->state.alarm == 2 && !model->zero(1));
    CHECK(model->unlock());
    model->tick(505);
    CHECK(model->state.recovering && capture->bytes[capture->b - 1] == 0x18 && capture->n == 2);
    model->begin(600); // Its own reset greeting must preserve the explicit recovery request.
    model->receive("[VER:1.1f.20261020:]", 601);
    model->receive("[FIRMWARE:grblHAL]", 602);
    model->receive("<Alarm:11>", 603);
    model->tick(900);
    for (int i=0;i<5;i++) { model->receive("ok", 901+i); model->tick(901+i); }
    int before = capture->n;
    model->receive("<Alarm:11>", 3400); model->tick(3400);
    CHECK(model->state.recovering && capture->n == before);
    model->receive("<Alarm:11>", 3600); model->tick(3600);
    CHECK(!model->state.recovering && !strcmp(capture->lines[capture->n-1], "$X"));
    model->receive("ok", 3601); model->tick(3602);
    CHECK(capture->n == before+1);
    CHECK(model->stop() && capture->bytes[capture->b - 1] == 0x18);
    CHECK(!model->state.status);
    ready(); model->receive("<Alarm:10>", 500); CHECK(model->unlock());
    model->disconnect(); CHECK(!model->state.recovering);
    ready(); model->receive("<Alarm:11>", 500); before=capture->b;
    CHECK(model->unlock()); model->tick(501);
    CHECK(capture->b == before && !strcmp(capture->lines[capture->n-1], "$X"));
    model->receive("ok",502);model->tick(503);
    return true;
}
static bool sd_download() {
    ready();
    capture->downloaded = 0;
    capture->fail_download = false;
    CHECK(!model->download("/missing.nc", download_data, capture));
    CHECK(model->list());
    model->tick(500);
    model->receive("[FILE:/preview.nc|SIZE:100]", 501);
    model->receive("[FILE:/large.nc|SIZE:3000000]", 501);
    model->receive("[FILE:/bad~.nc|SIZE:50]", 501);
    model->receive("[FILE:/unusable.nc|SIZE:50|UNUSABLE]", 501);
    model->receive("ok", 501);
    model->tick(502);
    CHECK(!model->download("/large.nc", download_data, capture));
    CHECK(!model->download("/bad~.nc", download_data, capture) && !model->run("/bad~.nc"));
    CHECK(!model->download("/unusable.nc", download_data, capture));
    CHECK(model->download("/preview.nc", download_data, capture));
    model->tick(503);
    CHECK(!strcmp(capture->lines[1], "$F<=/preview.nc"));
    CHECK(!model->jog('X', 1, 600) && !model->run("/preview.nc"));
    model->receive("G21 G90", 3000);
    model->tick(3001); // A long transfer is not a lost-status failure.
    CHECK(model->state.online && model->state.downloading);
    model->receive("<Idle|MPos:1,2,3>", 3002);
    model->receive("G1 X10 Y20", 3003);
    model->receive("%ok", 3004);
    model->tick(3005);
    CHECK(model->state.download_ready && !model->state.busy);
    CHECK(!strcmp(capture->download, "G21 G90\nG1 X10 Y20\n%\n"));
    CHECK(model->state.download_bytes == capture->downloaded);
    CHECK(!model->jog('X', 1, 600));
    model->receive("<Idle>", 3005);
    CHECK(model->download("/preview.nc", download_data, capture));
    model->tick(3006);
    capture->fail_download = true;
    model->receive("G1 X99", 3007);
    CHECK(model->state.downloading && model->state.busy);
    model->receive("ok", 3008);
    model->tick(3009);
    CHECK(!model->state.download_ready && !model->state.busy);
    model->receive("<Idle>", 3009);
    capture->fail_download = false;
    CHECK(model->download("/preview.nc", download_data, capture));
    model->tick(3010);
    model->tick(13011);
    CHECK(!model->state.online && !model->state.download_ready);
    return true;
}
static bool goto_work_zero() {
    ready();
    CHECK(!model->goto_zero(7));
    model->state.modal_known = false;
    CHECK(!model->goto_zero(3));
    model->state.modal_known = true;
    model->state.inches = true;
    model->state.absolute = false;
    model->state.machine[2] = -12;
    CHECK(model->goto_zero(3));
    CHECK(!model->jog('X', 1, 600));
    const char *commands[] = {"G21 G90",          "G53 G0 Z-5", "G0 X0", "G0 Y0",
                              "G53 G0 Z-12.0000", "G21",        "G91",   "$G"};
    for (int i = 0; i < 8; i++) {
        model->tick(500 + i);
        CHECK(!strcmp(capture->lines[i], commands[i]));
        model->receive("ok", 500 + i);
        if (i == 6)
            model->receive("<Idle>", 506);
    }
    model->tick(508);
    CHECK(model->state.busy);
    CHECK(!model->idle()); // Wait for fresh motion completion, not just command acknowledgements.
    model->receive("<Idle>", 509);
    model->tick(510);
    CHECK(model->idle());
    ready();
    CHECK(model->goto_zero(4));
    model->tick(500);
    model->receive("ok", 500);
    model->tick(501);
    CHECK(!strcmp(capture->lines[1], "G0 Z0"));
    model->receive("error:15", 502);
    model->tick(503);
    CHECK(capture->n == 2 && !model->state.busy && model->state.error);
    ready();
    CHECK(model->goto_zero(3));
    CHECK(model->stop());
    model->tick(500);
    CHECK(capture->n == 0);
    return true;
}
static bool jog_speed_presets() {
    ready();
    CHECK(!model->jog_preset("0+0", 1, 3));
    CHECK(!model->jog_preset("0+0", 1, 0));
    model->receive("$111=1000", 400);
    CHECK(!model->jog_preset("++0", 1, 0)); // Every moving axis needs a reported maximum.
    CHECK(model->jog_preset("0+0", 1, 0));
    model->tick(500);
    CHECK(!strcmp(capture->lines[0], "$J=G91 G21 Y1.000 F1000.0"));
    ready();
    model->receive("$110=6000", 400);
    model->receive("$111=3000", 401);
    model->receive("$112=1200", 402);
    model->receive("$110=nan", 403);
    CHECK(model->state.axis_max_rate[0] == 6000);
    CHECK(model->jog_preset("++0", 1, 0));
    model->tick(500);
    CHECK(!strcmp(capture->lines[0], "$J=G91 G21 X1.000 Y1.000 F3000.0"));
    model->receive("ok", 501);
    model->tick(502);
    CHECK(model->jog_preset("++0", 1, 1));
    model->tick(503);
    CHECK(strstr(capture->lines[1], "F1200.0"));
    model->receive("ok", 504);
    model->tick(505);
    CHECK(model->jog_preset("00+", 1, 2));
    model->tick(506);
    CHECK(!strcmp(capture->lines[2], "$J=G91 G21 Z1.000 F120.0"));
    model->receive("ok", 507); model->tick(508);
    model->receive("$112=2400", 509);
    CHECK(model->jog_preset("00+", 1, 2));
    model->tick(510);
    CHECK(!strcmp(capture->lines[3], "$J=G91 G21 Z1.000 F240.0"));
    return true;
}
static bool surface_sequence() {
    const int axes[] = {2, 0, 0, 1, 1}, directions[] = {-1, -1, 1, -1, 1};
    for (int test = 0; test < 5; ++test) {
        ready();
        ProbeSettings p; p.diameter = test ? 6 : 0; // Z never needs a tool diameter.
        int axis = axes[test], direction = directions[test];
        CHECK(!model->probe_surface(p, axis, direction));
        CHECK(model->contact_test(400));
        CHECK(model->state.contact_testing);
        model->receive("<Idle|Pn:I>", 401); model->tick(401);
        CHECK(model->contact_test(402)); // Reopening a modal preserves the release stage.
        model->receive("<Idle|Pn:i>", 403); model->tick(403);
        CHECK(model->state.contact_ready && !model->state.contact_testing);
        bool absolute = test % 2 == 0;
        model->state.absolute = absolute;
        CHECK(model->probe_surface(p, axis, direction));
        CHECK(!model->state.contact_ready);
        bool waiting = false;
        for (int n = 0; n < 12 && model->state.busy; ++n) {
            model->tick(500 + n * 10);
            if (capture->n <= n) break;
            const char *cmd = capture->lines[n];
            if (strstr(cmd, "G38.2")) {
                model->receive("ok", 501 + n * 10);
                model->tick(502 + n * 10);
                CHECK(capture->n == n + 1);
                waiting = true;
                model->receive("[PRB:1,2,3:1]", 503 + n * 10);
            } else model->receive("ok", 501 + n * 10);
        }
        model->tick(1000);
        CHECK(waiting && !model->state.probing && capture->n == 7);
        CHECK(!strcmp(capture->lines[0], "G21 G91"));
        char expected[96];
        snprintf(expected, sizeof expected, "G38.2 %c%.3f F100.0", "XYZ"[axis], direction * 25.f);
        CHECK(!strcmp(capture->lines[1], expected));
        float offset = axis == 2 ? 5 : -direction * 13.f;
        snprintf(expected, sizeof expected, "G10 L20 P0 %c%.3f", "XYZ"[axis], offset);
        CHECK(!strcmp(capture->lines[2], expected));
        snprintf(expected, sizeof expected, "G0 %c%.3f", "XYZ"[axis], -direction * 2.f);
        CHECK(!strcmp(capture->lines[3], expected));
        CHECK(!strcmp(capture->lines[4], absolute ? "G90" : "G91"));
        CHECK(!strcmp(capture->lines[5], "$G") && !strcmp(capture->lines[6], "$#"));
    }
    ready(); model->state.contact_ready = true;
    ProbeSettings p;
    CHECK(!model->probe_surface(p, 2, 1));
    CHECK(model->probe_surface(p, 2, -1));
    model->tick(500); model->receive("ok", 501); model->tick(502);
    model->receive("[PRB:0,0,0:0]", 503);
    CHECK(model->state.error && !model->state.probing && capture->n == 2); // No zero/retract after failed PRB.
    return true;
}
static bool corner_sequence() {
    ready();
    ProbeSettings p;
    p.diameter = 6;
    CHECK(!model->probe_corner(p));
    model->state.contact_ready = true;
    CHECK(model->probe_corner(p));
    bool x = false, y = false, waiting_checked = false;
    int probes = 0;
    for (int i = 0; i < 40 && model->state.busy; i++) {
        model->tick(500 + i * 10);
        if (capture->n == 0)
            continue;
        const char *cmd = capture->lines[capture->n - 1];
        if (strstr(cmd, "G38.2")) {
            int n = capture->n;
            model->receive("ok", 501 + i * 10);
            model->tick(502 + i * 10);
            CHECK(capture->n == n);
            waiting_checked = true;
            model->receive("[PRB:1,2,3:1]", 503 + i * 10);
            probes++;
        } else
            model->receive("ok", 501 + i * 10);
        if (!strcmp(cmd, "G10 P0 L20 X-13.000")) {
            x = true;
        }
        if (!strcmp(cmd, "G10 P0 L20 Y-13.000")) {
            y = true;
        }
    }
    model->tick(1000);
    CHECK(waiting_checked && probes == 3 && x && y && !model->state.probing);
    ready();
    model->state.contact_ready = true;
    CHECK(model->probe_corner(p));
    for (int i = 0; i < 4; i++) {
        model->tick(500 + i);
        model->receive("ok", 500 + i);
    }
    model->receive("[PRB:0,0,0:0]", 600);
    CHECK(!model->state.busy && model->state.error && !model->state.probing);
    return true;
}
static bool units_overrides_and_completion() {
    const float metric[] = {.1f, 1, 10, 100}, imperial[] = {.254f, 2.54f, 25.4f, 254};
    for (unsigned i = 0; i < 4; i++) {
        CHECK(fabsf(jog_step_mm(false, i) - metric[i]) < .001f);
        CHECK(fabsf(jog_step_mm(true, i) - imperial[i]) < .001f);
        CHECK(fabsf(imperial[i] / display_scale(true) - metric[i] / 10) < .001f);
    }
    CHECK(jog_step_mm(true, 4) == 0);
    ready();
    CHECK(!model->override_target(false, 120));
    model->receive("<Run|SD:42,/test.nc|Ov:100,100,100>", 500);
    CHECK(model->state.overrides_known && model->state.sd_active);
    CHECK(model->override_target(false, 123));
    CHECK(capture->b == 5 && capture->bytes[0] == 0x91 && capture->bytes[2] == 0x93);
    CHECK(!model->override_target(false, 130)); // Wait for actual feedback before another target.
    model->receive("<Run|Ov:123,100,100>", 501);
    CHECK(model->state.feed_override == 123 && model->state.overrides_known);
    capture->b = 0;
    CHECK(model->override_target(true, 89));
    CHECK(capture->b == 2 && capture->bytes[0] == 0x9B && capture->bytes[1] == 0x9D);
    model->receive("<Run|Ov:123,100,89>", 502);
    CHECK(!model->override_target(false, 201) && !model->override_target(true, 9));
    model->receive("[MSG:Program End]", 503);
    CHECK(model->state.sd_active && !model->state.job_complete);
    model->receive("<Idle>", 504);
    CHECK(!model->state.sd_active && model->state.job_complete);
    ready();
    model->state.file_count=1; strcpy(model->state.files[0].path,"/tiny.nc");
    CHECK(model->run("/tiny.nc")); model->tick(500);
    model->receive("ok",501); model->tick(502);
    model->receive("<Idle>",503); CHECK(!model->state.job_complete);
    model->receive("[MSG:Pgm End]",504); CHECK(!model->state.job_complete);
    model->receive("<Idle>",505); CHECK(model->state.job_complete && !model->state.sd_active);
    ready();
    model->receive("<Run|SD:42,/test.nc>", 500);
    CHECK(model->stop());
    model->receive("<Idle>", 501);
    CHECK(!model->state.job_complete);
    ready();
    model->receive("<Run|SD:42,/test.nc>", 500);
    model->receive("error:20", 501);
    model->receive("<Idle>", 502);
    CHECK(!model->state.job_complete && model->state.job_failed);
    ready();
    model->receive("<Run|SD:42,/test.nc>", 500);
    model->receive("<Alarm:1>", 501);
    CHECK(model->state.job_failed && model->unlock());
    model->receive("<Idle>", 502);
    CHECK(!model->state.job_complete);
    CHECK(model->stop());
    return true;
}
static bool lost_feedback_and_write_failure() {
    ready();
    model->tick(2500);
    CHECK(!model->state.online && !model->jog('X', 1, 600));
    model->receive("<Idle|MPos:0,0,0>", 2501);
    CHECK(!model->state.status);
    ready();
    capture->fail = true;
    CHECK(model->zero(1));
    model->tick(500);
    CHECK(model->state.error && !model->state.busy);
    capture->fail = false;
    ready();
    model->receive("<Door:0>", 500);
    CHECK(!model->zero(1) && !model->resume());
    ready();
    model->receive("<Alarm:11>", 500);
    CHECK(model->state.alarm == 11 && model->home());
    model->tick(501);
    CHECK(!strcmp(capture->lines[0], "$H"));
    return true;
}
} // namespace
bool model_self_test() {
    if (!capture) {
        capture = new Capture;
        model = new Model(line, rt, capture);
    }
    struct Test {
        const char *name;
        bool (*fn)();
    };
    const Test tests[] = {{"identification_and_realtime", identification},
                          {"coordinates_and_probe_pins", coordinates_and_probe_pins},
                          {"sd_and_progress", sd_and_progress},
                          {"sd_download", sd_download},
                          {"goto_work_zero", goto_work_zero},
                          {"jog_speed_presets", jog_speed_presets},
                          {"commands_and_faults", commands_and_faults},
                          {"corner_sequence", corner_sequence},
                          {"single_surface_sequence_and_quick_contact", surface_sequence},
                          {"units_overrides_and_completion", units_overrides_and_completion},
                          {"feedback_and_write_failure", lost_feedback_and_write_failure}};
    for (const auto &t : tests) {
        if (!t.fn())
            return false;
        printf("@UGTEST PASS %s\n", t.name);
    }
    return true;
}
} // namespace panel
