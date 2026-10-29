#pragma once
#include "panel_model.h"
#include <string.h>
namespace panel {
inline uint32_t status_led_color(const State &s) {
    if (s.alarm || s.error) return 0xFF0000;
    if (!s.online || !s.status) return 0x080808;
    if (s.job_complete) return 0x00FF00;
    if (s.probing || s.sd_active || s.macro_running || strcmp(s.mode, "Idle")) return 0xFF8000;
    return 0x00FF00;
}
void hardware_init();
void hardware_status(const State &state);
bool hardware_led_ready();
uint32_t hardware_led_color();
}
