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
};
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
    CHECK(model->pause() && capture->bytes[capture->b - 1] == '!');
    model->receive("<Hold:1>", 601);
    CHECK(!model->resume());
    model->receive("<Hold:0>", 602);
    CHECK(model->resume() && capture->bytes[capture->b - 1] == '~');
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
    CHECK(!strcmp(capture->lines[2], "$X"));
    CHECK(model->stop() && capture->bytes[capture->b - 1] == 0x18);
    CHECK(!model->state.status);
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
                          {"commands_and_faults", commands_and_faults},
                          {"corner_sequence", corner_sequence},
                          {"feedback_and_write_failure", lost_feedback_and_write_failure}};
    for (const auto &t : tests) {
        if (!t.fn())
            return false;
        printf("@UGTEST PASS %s\n", t.name);
    }
    return true;
}
} // namespace panel
