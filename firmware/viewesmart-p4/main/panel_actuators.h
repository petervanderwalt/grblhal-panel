#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace panel {
struct ActuatorConfig {
    char name[25], first[17], second[17];
    float end1 = 0, end2 = 0, feed = 100;
    uint8_t style = 0; // Buttons, position, presets, or repeating endpoint travel.
};
struct ActuatorPreferences {
    uint32_t magic = 0x41435431;
    ActuatorConfig axes[4] = {{"Actuator 1", "Close", "Open"}, {"Actuator 2", "Close", "Open"},
                              {"Actuator 3", "Close", "Open"}, {"Actuator 4", "Close", "Open"}};
    bool valid() const {
        if (magic != 0x41435431)
            return false;
        for (const auto &a : axes)
            if (!memchr(a.name, 0, sizeof a.name) || !a.name[0] ||
                !memchr(a.first, 0, sizeof a.first) || !a.first[0] ||
                !memchr(a.second, 0, sizeof a.second) || !a.second[0] ||
                !isfinite(a.end1) || !isfinite(a.end2) || !isfinite(a.feed) ||
                fabsf(a.end1) > 100000 || fabsf(a.end2) > 100000 || a.feed < .1f ||
                a.feed > 1000000 || a.style > 3)
                return false;
        return true;
    }
};
}
