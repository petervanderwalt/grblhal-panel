#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
namespace panel {
constexpr unsigned MaxMacros = 20, MaxMacroCode = 768;
struct Macro { char name[33] = ""; char code[MaxMacroCode + 1] = ""; uint8_t color = 0; };
struct Macros {
    uint32_t magic = 0x4D414331;
    Macro entries[MaxMacros];
    bool valid() const {
        if (magic != 0x4D414331) return false;
        for (const auto &m : entries)
            if (!memchr(m.name, 0, sizeof m.name) || !memchr(m.code, 0, sizeof m.code) || m.color > 3) return false;
        return true;
    }
};
// Validate the whole batch before sending any line; realtime bytes bypass G-code parsing.
inline bool macro_lines(const char *code, std::vector<std::string> &lines) {
    lines.clear();
    if (!code || strlen(code) > MaxMacroCode) return false;
    std::string line;
    bool comment = false, semicolon = false;
    for (const unsigned char *p = (const unsigned char *)code;; ++p) {
        unsigned ch = *p;
        if (!ch || ch == '\n' || ch == '\r') {
            if (comment) return false;
            if (!line.empty()) lines.push_back(line);
            line.clear(); semicolon = false;
            if (lines.size() > 32) return false;
            if (!ch) break;
        } else {
            if (ch < 32 || ch > 126 || strchr("!~?", ch)) return false;
            if (semicolon) continue;
            if (ch == ';') { semicolon = true; continue; }
            if (ch == '(') { if (comment) return false; comment = true; continue; }
            if (ch == ')') { if (!comment) return false; comment = false; continue; }
            if (!comment && ch != ' ') line += char(ch);
            if (line.size() > 192) return false;
        }
    }
    // Sender/system commands can change transport or reset the controller mid-batch.
    for (const auto &s : lines) if (strchr("$[%", s[0])) return false;
    return !lines.empty();
}
}
