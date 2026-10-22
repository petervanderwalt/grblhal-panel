#pragma once
#include "panel_model.h"
namespace panel {
enum class Action {
    List,
    Run,
    Preview,
    Jog,
    Zero,
    GotoZero,
    Wcs,
    Home,
    Unlock,
    Pause,
    Resume,
    Stop,
    JogCancel,
    Contact,
    Acknowledge
};
void runtime_init();
bool runtime_connect(int tx = 37, int rx = 38, int baud = 115200);
void runtime_disconnect();
void runtime_snapshot(State &state);
bool runtime_action(Action action, const char *text = nullptr, float a = 0, float b = 0);
bool runtime_probe(const ProbeSettings &settings);
void runtime_demo(bool on);
bool runtime_is_demo();
bool runtime_inject(const char *line);
void runtime_last_tx(char *buffer, size_t size);
bool model_self_test();
bool runtime_preview_acquire(const char **data, size_t *size);
void runtime_preview_release();
} // namespace panel
