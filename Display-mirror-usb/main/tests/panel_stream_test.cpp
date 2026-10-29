#include "panel_model.h"
#include "panel_stream.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include <new>
#endif
namespace panel {
bool stream_self_test() {
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            printf("@UGTEST FAIL stream:%d\n", __LINE__);                                                    \
            return false;                                                                                    \
        }                                                                                                    \
    } while (0)
    static std::vector<std::string> sent;
    sent.clear();
    auto send = [](void *c, const char *s) {
        ((std::vector<std::string> *)c)->push_back(s);
        return true;
    };
    Stream stream(send, &sent);
    auto file = [](const char *code) {
#ifdef ESP_PLATFORM
        return fmemopen((void *)code, strlen(code), "r");
#else
        FILE *f = tmpfile();
        if (f) {
            fputs(code, f);
            rewind(f);
        }
        return f;
#endif
    };
    std::string code = "G21\nG90\n";
    for (int i = 0; i < 100; i++)
        code += "G1X123.456Y87.654F1000\n";
    CHECK(stream.start(file(code.c_str()), code.size(), 0));
    stream.tick(1, true);
    CHECK(!stream.checking() && sent.empty());
    stream.tick(3, true);
    CHECK(stream.in_flight() < 128 && stream.outstanding() > 1);
    unsigned count = sent.size();
    stream.tick(4, false);
    CHECK(sent.size() == count);
    for (unsigned time = 5; !stream.done() && time < 1000; time++) {
        if (stream.outstanding())
            stream.acknowledge(time);
        stream.tick(time, true);
        CHECK(stream.in_flight() < 128);
    }
    CHECK(stream.done() && sent.size() == 102 && stream.acknowledged() == code.size());
    CHECK(stream.stats().sent_lines==102 && stream.stats().ack_lines==102);
    CHECK(stream.stats().sent_bytes==stream.stats().ack_bytes && stream.stats().high_water<128);
    stream.cancel();
    sent.clear();
    CHECK(stream.start(file("G1X1\nM3!\n"), 9, 0));
    stream.tick(1, true);
    CHECK(stream.failed() && sent.empty());
    CHECK(stream.start(file("G1X1\n"), 5, 0));
    stream.tick(1, true);
    stream.tick(2, true);
    CHECK(stream.outstanding() == 1);
    stream.tick(300003, true);
    CHECK(stream.failed());
#ifdef ESP_PLATFORM
    static Model &m = *new (heap_caps_malloc(sizeof(Model), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                          Model(send, [](void *, uint8_t) { return true; }, &sent);
#else
    static Model m(send, [](void *, uint8_t) { return true; }, &sent);
#endif
    auto ready = [&] {
        m.begin(0);
        m.receive("[VER:1.1f.20261020:]", 1);
        m.receive("[FIRMWARE:grblHAL]", 2);
        m.receive("<Idle|FS:0,0>", 3);
        m.tick(300);
        for (int i = 0; i < 5; i++) {
            m.receive("ok", 301 + i);
            m.tick(301 + i);
        }
        sent.clear();
    };
    ready();
    FILE *f = file("G21\nG1X1\nM30\n");
    CHECK(f && m.run_local(f, "/test.nc", 13, 400));
    m.tick(401);
    CHECK(m.state.local_job && !m.state.job_preparing);
    m.tick(402);
    CHECK(sent.size() > 1 && !m.state.job_complete);
    m.receive("<Idle>", 403);
    CHECK(!m.state.job_complete);
    for (unsigned i = 0; i < 3; i++)
        m.receive("ok", 404 + i);
    m.tick(408);
    CHECK(!m.state.job_complete);
    m.receive("<Run>", 409);
    CHECK(!m.state.job_complete);
    m.receive("<Idle>", 410);
    CHECK(m.state.job_complete && !m.state.local_job);
    ready();
    f = file("G1X1\nG1X2\n");
    CHECK(m.run_local(f, "/test.nc", 10, 400));
    m.tick(401);
    m.tick(402);
    m.receive("error:20", 403);
    CHECK(m.state.job_failed && !m.state.local_job && !m.state.job_complete);
    auto before_lookup=sent.size(); m.tick(800); CHECK(sent.size()==before_lookup);
    m.receive("error:9",801); CHECK(m.state.error==20); // Drain the cancelled block, preserving the first fault.
    m.tick(1000); CHECK(sent.size()==before_lookup);
    m.tick(1100); CHECK(sent.size()==before_lookup);
    CHECK(m.state.error==20 && m.state.job_failed && !m.state.job_complete);
    ready();
    f = file("G1X1\n");
    CHECK(m.run_local(f, "/test.nc", 5, 400));
    m.begin(401);
    CHECK(!m.state.local_job && m.state.job_failed && m.state.error);
    printf("@UGTEST PASS display_sd_stream_window_validation_hold_errors_reset_and_fresh_idle\n");
    return true;
#undef CHECK
}
} // namespace panel
