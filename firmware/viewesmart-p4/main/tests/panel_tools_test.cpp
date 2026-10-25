#include "panel_model.h"
#include "panel_macros.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
namespace panel {
bool tools_self_test() {
#define CHECK(x) do { if (!(x)) { printf("@UGTEST FAIL tools:%d\n", __LINE__); return false; } } while (0)
    static char last[256];
    static uint8_t last_rt;
    static Model m([](void *, const char *s) { snprintf(last, sizeof last, "%s", s); return true; },
                   [](void *, uint8_t b) { last_rt = b; return true; }, nullptr);
    auto ready = [&] {
        m.begin(0); m.receive("[VER:1.1f.20261020:]", 1); m.receive("[FIRMWARE:grblHAL]", 2);
        m.receive("<Idle|MPos:0,0,0|FS:0,0|Pn:i>", 3); m.tick(300);
        for (int i = 0; i < 5; ++i) { m.receive("ok", 301+i); m.tick(301+i); }
    };
    ready();
    CHECK(m.spindle_list()); m.tick(350); CHECK(!strcmp(last, "$SPINDLESH"));
    m.receive("[SPINDLE:0|0|0|V*|PWM|0,24000]", 351);
    m.receive("[SPINDLE:7|1|2|VD|RS485 VFD|1000,18000]", 351);
    m.receive("[SPINDLE:2|-|0|V|Disabled|0,1000]", 351);
    m.receive("[SPINDLE:0|bad|0|V|Malformed|0,1000]", 351);
    m.receive("ok", 352); m.tick(353);
    CHECK(m.state.spindle_count == 3 && m.state.spindles_result == 1 && !m.state.spindles_loading);
    CHECK(m.state.spindles[0].active && m.state.spindle_max == 24000 && m.state.spindles[2].number == -1);
    CHECK(!m.spindle_select(-1) && !m.spindle_select(7) && !m.spindle_select(1)); // No accessory feedback yet.
    m.receive("<Idle|A:S|FS:0,1000>", 354); CHECK(!m.spindle_select(1));
    m.receive("<Idle|A:|FS:0,0>", 355); CHECK(m.spindle_select(1));
    m.tick(356); CHECK(!strcmp(last, "M104Q1") && !m.state.spindle_limits_known);
    m.receive("ok", 357); m.tick(358); CHECK(!strcmp(last, "$SPINDLESH"));
    m.receive("[SPINDLE:7|1|2|VD*|RS485 VFD|1000,18000]", 359);
    m.receive("ok", 360); m.tick(361);
    CHECK(m.state.spindles[0].active && m.state.spindle_min == 1000 && m.state.spindle_max == 18000);
    CHECK(!m.spindle_control(1, 20000));
    CHECK(m.spindle_list()); m.tick(362); m.receive("error:3", 363);
    CHECK(!m.state.error && !m.state.busy && m.state.spindles_result == -1);
    ready();
    m.receive("<Idle|Pn:XDIJ>", 400);
    CHECK((m.state.inputs & (1u << ('X'-'A'))) && m.state.probe);
    m.receive("<Idle|Pn:j>", 401);
    CHECK(!(m.state.inputs & (1u << ('X'-'A'))) && !(m.state.inputs & (1u << ('J'-'A'))) && m.state.probe);
    m.receive("<Idle>", 402); CHECK(m.state.probe && (m.state.inputs & (1u << ('I'-'A'))));
    m.receive("<Idle|Pn:i>", 403); CHECK(!m.state.probe);
    m.receive("<Idle|A:SCFM>", 404); CHECK(m.state.accessories_known && m.state.spindle_direction == -1 && m.state.coolant == 3);
    m.receive("<Idle>", 405); CHECK(m.state.coolant == 3);
    m.receive("<Idle|A:>", 406); CHECK(!m.state.coolant && !m.state.spindle_direction);
    CHECK(!m.spindle_control(1, 12000));
    m.receive("$30=24000", 407); CHECK(!m.state.spindle_limits_known); m.receive("$31=1000", 408);
    CHECK(!m.spindle_control(1, 999) && !m.spindle_control(-1, 24001) && !m.spindle_control(1, NAN));
    CHECK(m.spindle_control(-1, 12000)); m.tick(409); CHECK(!strcmp(last, "M4 S12000"));
    m.receive("ok", 410); m.tick(411);
    CHECK(m.coolant_control(3));
    m.tick(412); CHECK(!strcmp(last, "M9")); m.receive("ok", 413);
    m.tick(414); CHECK(!strcmp(last, "M7")); m.receive("ok", 415);
    m.tick(416); CHECK(!strcmp(last, "M8")); m.receive("ok", 417); m.tick(418);
    std::vector<std::string> lines;
    CHECK(macro_lines(";comment\nG21 (mm)\nG0 X1\nM5\n", lines) && lines.size() == 3);
    CHECK(!macro_lines("G0 X1\nM8!", lines) && !macro_lines("$RST=*", lines));
    CHECK(!macro_lines("(missing", lines) && !macro_lines(";empty", lines));
    CHECK(!m.macro_run("M5\nM8!")); CHECK(!m.state.busy);
    CHECK(m.macro_run("M5\nM8"));
    m.tick(420); CHECK(!strcmp(last, "M5"));
    CHECK(!m.macro_run("M9"));
    m.receive("ok", 421); m.tick(422); CHECK(!strcmp(last, "M8"));
    m.receive("ok", 423); m.tick(424); CHECK(!strcmp(last, "$G") && !m.state.modal_known);
    m.receive("[GC:G1 G55 G17 G21 G91 M5 M9 T0 F100 S0]", 425);
    m.receive("ok", 426); m.tick(427); CHECK(m.state.busy && m.state.macro_running && !m.state.absolute && m.state.wcs == 55);
    m.receive("<Run>", 428); CHECK(m.state.macro_running);
    m.receive("<Idle>", 429); m.tick(430); CHECK(!m.state.busy && !m.state.macro_running && m.state.modal_known);
    CHECK(m.macro_run("M5\nM8")); m.tick(440); m.receive("error:20", 441);
    CHECK(!m.state.busy && !m.state.macro_running && !m.spindle_control(1, 12000));
    CHECK(last_rt == 0x82); // Hold any previously accepted motion after an error.
    CHECK(m.state.macro_failed && !m.idle());
    CHECK(m.stop() && !m.state.macro_failed && !m.state.status);
    ready();
    CHECK(m.macro_run("G38.2Z-10F50")); m.tick(450); m.receive("ok", 451); m.tick(452);
    CHECK(!strcmp(last, "$G")); m.receive("ok", 453);
    m.receive("<Idle>", 454); m.tick(455); CHECK(!m.state.busy);
    m.receive("$10=0", 456); CHECK(m.state.pins_reporting_known && !m.state.pins_reporting && !m.state.inputs_known);
    m.receive("$10=511", 457); m.receive("<Idle>", 458); CHECK(m.state.inputs_known & (1u << ('X'-'A')));
    std::string too_many;
    for (int i = 0; i < 33; ++i) too_many += "M5\n";
    CHECK(!m.macro_run(too_many.c_str()) && !m.state.busy);
    ready(); m.receive("[VER:1.1f.20240101:]", 450); m.state.extended = false;
    m.receive("<Idle|Pn:XP>", 451); CHECK(m.state.probe);
    m.receive("<Idle>", 452); CHECK(!m.state.probe && !(m.state.inputs & (1u << ('X'-'A'))));
    m.disconnect(); CHECK(!m.macro_run("M5"));
    puts("@UGTEST PASS inputs, accessory controls, validated macros and fresh-Idle completion");
    return true;
#undef CHECK
}
}
