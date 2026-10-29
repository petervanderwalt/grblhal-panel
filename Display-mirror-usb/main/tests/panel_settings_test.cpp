#include "panel_model.h"
#include <cstdio>
#include <cstring>
namespace panel {
bool settings_self_test() {
#define CHECK(x) do { if (!(x)) { printf("@UGTEST FAIL settings:%d\n", __LINE__); return false; } } while (0)
    SettingsCatalog catalog;
    CHECK(catalog.receive("[SETTINGGROUP:43|0|Axes]"));
    CHECK(catalog.receive("[SETTINGGROUP:45|43|X axis]"));
    CHECK(catalog.receive("[SETTING:100|45|X steps|steps/mm|6|0.000|1|1000|1|0]"));
    CHECK(!catalog.find(100)->has_value && catalog.find(100)->reboot);
    CHECK(catalog.receive("$100=250.000"));
    CHECK(catalog.receive("100\tX steps\tsteps/mm\tfloat\t0.000\tTravel resolution\\nCalibrate before use\t1\t1000"));
    CHECK(catalog.find(100)->description.find('\n') != std::string::npos);
    CHECK(catalog.in_group(45, 43) && !catalog.in_group(43, 45));
    std::string error;
    CHECK(SettingsCatalog::validate(*catalog.find(100), "250.123", error));
    CHECK(!SettingsCatalog::validate(*catalog.find(100), "1001", error));
    CHECK(!SettingsCatalog::validate(*catalog.find(100), "nan", error));
    CHECK(!SettingsCatalog::validate(*catalog.find(100), "2\n$X", error));
    CHECK(catalog.receive("[SETTING:75|16|Password||8|x(64)|0|64|1|1]"));
    CHECK(catalog.receive("$75=private-test-value"));
    CHECK(catalog.find(75)->value == "********");
    CHECK(catalog.receive("[SETTING:32|1|Mode||3|Normal,N/A,Laser|||0|0]"));
    CHECK(catalog.receive("$32=0"));
    CHECK(!SettingsCatalog::validate(*catalog.find(32), "1", error));
    CHECK(SettingsCatalog::validate(*catalog.find(32), "2", error));
    CHECK(catalog.receive("[SETTING:23|11|Axes||4|4|0|15|0|0]"));
    CHECK(SettingsCatalog::options(*catalog.find(23), 3).size() == 4);
    CHECK(catalog.receive("[SETTING:23|11|Axes||4|4|||0|0]"));
    CHECK(catalog.receive("$23=3"));
    CHECK(SettingsCatalog::validate(*catalog.find(23), "8", error));
    CHECK(!SettingsCatalog::validate(*catalog.find(23), "16", error));
    CHECK(catalog.receive("[SETTING:302|15|IP||9||||1|0]")); CHECK(catalog.receive("$302=192.168.0.1"));
    CHECK(!SettingsCatalog::validate(*catalog.find(302), "192.168.999.1", error));
    CHECK(SettingsCatalog::matches(*catalog.find(302), "192.168.000.001"));
    CHECK(catalog.receive(("$74=" + std::string(MaxSettingValue + 1, 'a')).c_str()));
    CHECK(catalog.receive("[SETTING:74|16|SSID||7|x(64)|0|64|0|0]"));
    CHECK(!catalog.find(74)->writable);
    CHECK(!SettingsCatalog::validate(*catalog.find(74), "short", error));
    struct Capture {
        char last[640] = {};
        static bool line(void *p, const char *s) { snprintf(((Capture *)p)->last, 640, "%s", s); return true; }
        static bool rt(void *, uint8_t) { return true; }
    };
    static Capture capture;
    static Model *mp = new Model(Capture::line, Capture::rt, &capture);
    auto &m = *mp;
    auto ready = [&] {
        m.begin(0); m.receive("[VER:1.1f.20261020:]", 1); m.receive("[FIRMWARE:grblHAL]", 2);
        m.receive("<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0>", 3); m.tick(300);
        for (int i = 0; i < 5; i++) { m.receive("ok", 301 + i); m.tick(301 + i); }
    };
    ready(); CHECK(m.settings_load());
    m.receive("$13=0", 400);
    m.receive("<Idle|MPos:25,0,0|WCO:0,0,0>", 401);
    m.receive("$13=1", 402);
    CHECK(m.state.machine[0] == 25);
    m.receive("$13=0", 403);
    CHECK(m.state.machine[0] == 25);
    m.tick(500); CHECK(!strcmp(capture.last, "$EG"));
    m.receive("[SETTINGGROUP:35|0|Stepper]", 501); m.receive("ok", 502); m.tick(503);
    CHECK(!strcmp(capture.last, "$ES"));
    m.receive("[SETTING:0|35|Step pulse|us|6|0.0|2|20|0|0]", 504);
    m.receive("ok", 505); m.tick(506); CHECK(!strcmp(capture.last, "$ESH"));
    m.receive("0\tStep pulse\tus\tfloat\t0.0\tPulse duration\t2\t20", 507);
    m.receive("ok", 508); m.tick(509); CHECK(!strcmp(capture.last, "$$"));
    m.receive("$0=10.0", 510); m.receive("ok", 511); m.tick(512);
    CHECK(m.state.settings_ready && m.state.settings_result == 1 && !m.state.settings_loading);
    CHECK(!m.setting_write(0, "99"));
    CHECK(m.setting_write(0, "12.0")); m.tick(513); CHECK(!strcmp(capture.last, "$0=12.0"));
    CHECK(m.state.settings_saving && !m.state.settings_ready);
    const char *queries[] = {"$EG", "$ES", "$ESH", "$$"};
    for (int i = 0; i < 4; i++) { m.receive("ok", 514 + i * 2); m.tick(515 + i * 2); CHECK(!strcmp(capture.last, queries[i])); }
    m.receive("[SETTING:0|35|Step pulse|us|6|0.0|2|20|0|0]", 523);
    m.receive("$0=12.0", 524); m.receive("ok", 525); m.tick(526);
    CHECK(m.state.settings_result == 1 && m.settings.find(0)->value == "12.0");
    CHECK(m.setting_write(0, "12.5")); m.tick(526);
    for (int i = 0; i < 4; i++) { m.receive("ok", 526); m.tick(526); }
    m.receive("[SETTING:0|35|Step pulse|us|6|0.0|2|20|0|0]", 526);
    m.receive("$0=11.0", 526); m.receive("ok", 526); m.tick(526);
    CHECK(m.state.settings_result == -1 && strstr(m.state.settings_message, "did not match"));
    m.receive("<Run|MPos:0,0,0>", 527); CHECK(!m.setting_write(0, "10.0"));
    m.receive("<Idle|MPos:0,0,0>", 528);
    CHECK(m.setting_write(0, "13.0")); m.tick(529); m.receive("error:3", 530);
    CHECK(m.state.settings_result == -1 && !m.state.settings_saving);
    ready(); CHECK(!m.setting_write(0, "10.0"));
    CHECK(!m.state.settings_ready && m.settings.entries.empty());
    m.settings.receive("[SETTING:75|16|Password||8|x(64)|0|64|1|1]");
    m.settings.receive("$75=********"); m.state.settings_ready = true;
    CHECK(m.setting_write(75, "private-test-value")); m.tick(600); m.receive("error:3", 601);
    CHECK(!strstr(m.state.message, "private-test-value"));
    ready();
    m.settings.receive("[SETTING:13|1|Report inches||0||||0|0]");
    m.receive("$13=0", 600); m.state.settings_ready = true;
    CHECK(m.setting_write(13, "1")); m.tick(601);
    m.receive("<Idle|MPos:1,0,0|WCO:0,0,0>", 602);
    CHECK(m.state.coordinates);
    m.receive("$13=1", 603);
    CHECK(!m.state.coordinates && !m.state.wco_known);
    m.receive("<Idle|MPos:1,0,0|WCO:0,0,0>", 604);
    CHECK(m.state.coordinates && m.state.machine[0] > 25.399f && m.state.machine[0] < 25.401f);
    ready(); m.receive("<Idle|MPos:0,0,0>", 20000); m.tick(20000);
    CHECK(m.settings_load()); m.tick(20001);
    CHECK(!strcmp(capture.last, "$EG") && m.state.settings_loading);
    m.receive("[SETTINGGROUP:35|0|Stepper]", 29000); m.tick(29501);
    CHECK(m.state.online && m.state.settings_loading);
    m.tick(39001);
    CHECK(!m.state.online && m.state.settings_result == -1 && !m.state.settings_loading);
    printf("@UGTEST PASS grouped_settings_metadata_types_validation_and_readback\n");
    return true;
#undef CHECK
}
}
