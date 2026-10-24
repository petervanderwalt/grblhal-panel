#include "panel_model.h"
#include "panel_actuators.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace panel {
namespace {
struct ActuatorCapture {
    char last[192] = {};
    uint8_t last_rt = 0;
    unsigned lines = 0;
    static bool line(void *ctx, const char *s) {
        snprintf(((ActuatorCapture *)ctx)->last, 192, "%s", s);
        ((ActuatorCapture *)ctx)->lines++;
        return true;
    }
    static bool realtime(void *ctx, uint8_t byte) { ((ActuatorCapture *)ctx)->last_rt = byte; return true; }
};
}
bool actuator_self_test() {
#define REQUIRE(x) do { if (!(x)) { printf("@UGTEST FAIL actuators:%d\n", __LINE__); return false; } } while (0)
    static ActuatorCapture c;
    static Model m(ActuatorCapture::line, ActuatorCapture::realtime, &c);
    auto ready = [&] {
        m.begin(0);
        m.receive("[VER:1.1f.20261020:]", 1);
        m.receive("[FIRMWARE:grblHAL]", 2);
        m.receive("$376=0", 2);
        m.receive("<Idle|MPos:0,0,0,0|WCO:1,2,3,4|H:0,15|FS:0,0>", 3);
        m.tick(300);
        for (int i = 0; i < 5; i++) { m.receive("ok", 301 + i); m.tick(301 + i); }
    };
    ready();
    REQUIRE(m.state.axis_count == 4 && m.state.machine_known && m.state.homed_axes == 15);
    REQUIRE(m.state.work[3] == -4);
    REQUIRE(!m.actuator_move(3, -101, 100, 0, -100));
    REQUIRE(!m.actuator_move(3, NAN, 100, 0, -100));
    REQUIRE(!m.actuator_move(3, -50, 100, 0, 0));
    m.state.axis_max_rate[3] = 200;
    REQUIRE(m.actuator_move(3, -50, 300, 0, -100));
    REQUIRE(!m.actuator_move(0, -10, 100, 0, -100)); // No queued concurrent move.
    m.tick(500);
    REQUIRE(!strcmp(c.last, "$J=G53 G90 G21 A-50.000 F200.0"));
    m.receive("ok", 501); m.tick(502);
    REQUIRE(!m.actuator_move(0, -10, 100, 0, -100)); // ACK is not motion complete.
    m.receive("<Idle|MPos:0,0,0,-50|H:0,15>", 503);
    REQUIRE(m.actuator_move(0, -10, 100, 0, -100));
    ready();
    m.receive("<Idle|MPos:0,0,0|H:1>", 500);
    REQUIRE(!m.actuator_move(3, -50, 100, 0, -100));
    m.receive("<Idle|MPos:0,0,0,0|H:0,0>", 501);
    REQUIRE(!m.actuator_move(0, -50, 100, 0, -100));
    REQUIRE(m.actuator_home('A'));
    m.tick(502); REQUIRE(!strcmp(c.last, "$HA"));
    m.receive("ok", 503); m.tick(504); REQUIRE(!strcmp(c.last, "$#"));
    m.receive("[HOME:0,0,0,0:8]", 505);
    m.receive("ok", 506); m.tick(507);
    m.receive("<Idle|MPos:0,0,0,0>", 508);
    REQUIRE(m.state.homed_axes == 8);
    REQUIRE(m.actuator_move(3, -50, 100, 0, -100));
    m.receive("ALARM:1", 509);
    REQUIRE(!m.state.homed_axes && !m.actuator_move(3, -50, 100, 0, -100));
    ready();
    m.receive("$13=1", 500);
    REQUIRE(m.actuator_move(3, -50, 100, 0, -100)); // Confirmed linear A still converts inch feedback.
    ready();
    m.receive("$376=1", 500);
    REQUIRE(!m.state.machine_known);
    m.receive("$13=1", 501);
    m.receive("<Idle|MPos:1,2,3,-90|WCO:0,0,0,10|H:0,15>", 502);
    REQUIRE(fabsf(m.state.machine[0] - 25.4f) < .001f);
    REQUIRE(m.state.machine[3] == -90 && m.state.wco[3] == 10 && m.state.work[3] == -100);
    REQUIRE(!m.actuator_move(3, -45, 100, 0, -180)); // Type mismatch.
    REQUIRE(!m.actuator_move(0, -45, 100, 0, -180, true)); // Rotary is A-only.
    m.receive("$13=0", 503);
    REQUIRE(m.state.machine[3] == -90 && m.state.wco[3] == 10);
    REQUIRE(m.actuator_move(3, -45, 100, 0, -180, true));
    m.tick(504);
    REQUIRE(!strcmp(c.last, "$J=G53 G90 G21 A-45.000 F100.0"));
    m.disconnect(); REQUIRE(!m.state.homed_axes && !m.state.machine_known);
    ready(); REQUIRE(m.actuator_home('X')); m.tick(500);
    m.receive("<Home|MPos:0,0,0,0>", 16000); m.tick(16000);
    REQUIRE(m.state.busy && !m.state.error); // Homing is not a ten-second normal command.
    m.receive("<Home|MPos:0,0,0,0>", 300501); m.tick(300501);
    REQUIRE(m.state.error && c.last_rt == 0x18 && !m.state.homed_axes);
    ActuatorPreferences p;
    REQUIRE(p.valid());
    p.axes[0].feed = NAN; REQUIRE(!p.valid());
    p = ActuatorPreferences{}; p.axes[2].style = 3; REQUIRE(p.valid());
    p.axes[2].style = 4; REQUIRE(!p.valid());
    ready();
    REQUIRE(m.actuator_cycle(0, 100, 0, 100));
    REQUIRE(!m.actuator_cycle(1, 100, 0, 100));
    m.tick(500); REQUIRE(strstr(c.last, "X100.000 F100.0"));
    REQUIRE(m.actuator_cycle_speed(100)); // Defer a slider release until the outstanding ACK.
    m.receive("ok", 501); m.tick(502);
    REQUIRE(c.last_rt == 0x85);
    unsigned lines = c.lines;
    REQUIRE(m.state.cycle_axis == 0 && !m.idle());
    m.tick(503); REQUIRE(c.lines == lines); // ACK alone never reverses direction.
    m.receive("<Idle|MPos:100,0,0,0|H:0,15>", 504);
    m.tick(505); REQUIRE(strstr(c.last, "X0.000 F100.0"));
    m.receive("ok", 506); m.tick(507);
    m.receive("<Jog|MPos:75,0,0,0|H:0,15>", 508);
    REQUIRE(m.actuator_cycle_speed(50) && c.last_rt == 0x85);
    lines = c.lines;
    m.receive("<Idle|FS:0,0>", 509); m.tick(510);
    REQUIRE(c.lines == lines && !m.actuator_move(1, 50, 100, 0, 100));
    m.receive("<Idle|MPos:75,0,0,0|H:0,15>", 511);
    m.tick(512); REQUIRE(strstr(c.last, "X0.000 F50.0"));
    m.receive("ok", 513); m.tick(514);
    m.receive("<Idle|MPos:0,0,0,0|H:0,15>", 515);
    m.tick(516); REQUIRE(strstr(c.last, "X100.000 F50.0"));
    REQUIRE(m.jog_cancel() && m.state.cycle_axis == -1);
    lines = c.lines;
    m.receive("<Idle|MPos:20,0,0,0|H:0,15>", 517); m.tick(518);
    REQUIRE(c.lines == lines); // Stop disarms repetition permanently.
    ready(); REQUIRE(m.actuator_cycle(0, 100, 0, 100));
    m.tick(500); m.receive("ok", 501); m.tick(502);
    m.receive("<Hold:0|MPos:20,0,0,0|H:0,15>", 503);
    REQUIRE(m.state.cycle_axis == -1 && !strcmp(m.state.mode, "Hold:0"));
    ready(); REQUIRE(m.actuator_cycle(0, 100, 0, 100));
    m.receive("ALARM:1", 500); REQUIRE(m.state.cycle_axis == -1);
    ready(); REQUIRE(m.actuator_cycle(0, 100, 0, 100));
    m.tick(3001); REQUIRE(m.state.cycle_axis == -1 && !m.state.online);
    ready();
    m.receive("$376=6", 500); // Preserve B/C while changing A.
    unsigned previous_lines = c.lines;
    REQUIRE(m.actuator_type(false));
    m.tick(501); REQUIRE(c.lines == previous_lines); // Matching type does not rewrite NVS.
    REQUIRE(m.actuator_type(true));
    REQUIRE(!m.state.rotary_known && !m.state.machine_known && !(m.state.homed_axes & 8));
    REQUIRE(!m.actuator_move(3, -45, 100, -90, 90, true));
    m.tick(502); REQUIRE(!strcmp(c.last, "$376=7"));
    m.receive("ok", 503); m.tick(504); REQUIRE(!strcmp(c.last, "$$"));
    m.receive("$376=7", 505);
    m.receive("ok", 506); m.tick(507);
    REQUIRE(m.state.rotary_known && m.state.rotary_axes == 56);
    REQUIRE(m.actuator_type(false));
    m.tick(508); REQUIRE(!strcmp(c.last, "$376=6"));
    m.receive("error:3", 509);
    REQUIRE(!m.state.rotary_known && m.state.error && !m.actuator_type(true));
    ready(); m.state.rotary_known = false; REQUIRE(!m.actuator_type(true));
    ready(); m.receive("<Idle|MPos:0,0,0|H:0,7>", 500); REQUIRE(!m.actuator_type(true));
    ready(); REQUIRE(m.actuator_cycle(0, 100, 0, 100)); REQUIRE(!m.actuator_type(true));
    printf("@UGTEST PASS actuators_machine_coordinates_homing_bounds_and_settings\n");
    return true;
#undef REQUIRE
}
}
