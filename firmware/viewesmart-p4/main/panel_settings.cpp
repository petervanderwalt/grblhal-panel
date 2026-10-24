#include "panel_settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace panel {
namespace {
std::vector<std::string> split(const std::string &text, char delimiter) {
    std::vector<std::string> fields;
    size_t start = 0, end;
    do {
        end = text.find(delimiter, start);
        fields.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        start = end + 1;
    } while (end != std::string::npos);
    return fields;
}
bool identifier(const std::string &s, int &id) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) return false;
    unsigned long n = strtoul(s.c_str(), nullptr, 10);
    if (n > 65535) return false;
    id = int(n); return true;
}
bool number(const std::string &s, double &value) {
    if (s.empty()) return false;
    char *end;
    value = strtod(s.c_str(), &end);
    return end != s.c_str() && !*end && std::isfinite(value);
}
bool ip(const std::string &s) {
    auto parts = split(s, '.');
    if (parts.size() != 4) return false;
    for (const auto &p : parts) { int n; if (!identifier(p, n) || n > 255) return false; }
    return true;
}
std::string help(std::string s) {
    size_t at = 0;
    while ((at = s.find("\\n", at)) != std::string::npos) s.replace(at, 2, "\n");
    return s.substr(0, 2048);
}
}
void SettingsCatalog::clear() { groups.clear(); entries.clear(); overflow = false; }
const MachineSetting *SettingsCatalog::find(int id) const {
    for (const auto &s : entries) if (s.id == id) return &s;
    return nullptr;
}
MachineSetting *SettingsCatalog::entry(int id) {
    for (auto &s : entries) if (s.id == id) return &s;
    if (entries.size() >= 1024) { overflow = true; return nullptr; }
    entries.emplace_back(); entries.back().id = id;
    entries.back().name = "Setting " + std::to_string(id);
    return &entries.back();
}
bool SettingsCatalog::receive(const char *line) {
    std::string text(line);
    if (text.compare(0, 14, "[SETTINGGROUP:") == 0 && text.back() == ']') {
        auto f = split(text.substr(14, text.size() - 15), '|');
        int id, parent;
        if (f.size() < 3 || !identifier(f[0], id) || !identifier(f[1], parent)) return false;
        for (auto &g : groups) if (g.id == id) { g.parent = parent; g.name = f[2].substr(0, 128); return true; }
        if (groups.size() >= 128) { overflow = true; return true; }
        groups.push_back({id, parent, f[2].substr(0, 128)}); return true;
    }
    if (text.compare(0, 9, "[SETTING:") == 0 && text.back() == ']') {
        auto f = split(text.substr(9, text.size() - 10), '|');
        int id, group, type;
        if (f.size() < 8 || !identifier(f[0], id) || !identifier(f[1], group) || !identifier(f[4], type)) return false;
        auto s = entry(id); if (!s) return true;
        s->group = group; s->type = type; s->metadata = true;
        s->name = f[2].substr(0, 256); s->unit = f[3].substr(0, 64);
        s->format = f[5].substr(0, 1024); s->minimum = f[6]; s->maximum = f[7];
        // $ES fields 8/9 are reboot/allow-null flags, not a setting value.
        s->reboot = f.size() > 8 && f[8] == "1";
        s->allow_null = f.size() > 9 && f[9] == "1";
        s->writable = type <= 9 && f[5].size() <= 1024 && !s->oversized_value;
        if (type == 8 && s->has_value) s->value = "********";
        return true;
    }
    if (text.compare(0, 14, "[SETTINGDESCR:") == 0 && text.back() == ']') {
        size_t bar = text.find('|'); int id;
        if (bar != std::string::npos && identifier(text.substr(14, bar - 14), id))
            if (auto s = entry(id)) s->description = help(text.substr(bar + 1, text.size() - bar - 2));
        return true;
    }
    size_t tab = text.find('\t'); int id;
    if (tab != std::string::npos && identifier(text.substr(0, tab), id)) {
        auto f = split(text, '\t');
        if (f.size() >= 6) if (auto s = entry(id)) s->description = help(f[5]);
        return true;
    }
    size_t equal = text.find('=');
    if (text.size() > 2 && text[0] == '$' && equal != std::string::npos && identifier(text.substr(1, equal - 1), id)) {
        auto s = entry(id); if (!s) return true;
        std::string value = text.substr(equal + 1);
        s->oversized_value = value.size() > MaxSettingValue;
        s->writable = s->writable && !s->oversized_value;
        s->value = s->type == 8 ? "********" : value.substr(0, MaxSettingValue);
        s->has_value = true; return true;
    }
    return false;
}
bool SettingsCatalog::in_group(int member, int selected) const {
    if (selected < 0) return true;
    for (size_t depth = 0; depth <= groups.size(); depth++) {
        if (member == selected) return true;
        const SettingGroup *found = nullptr;
        for (const auto &g : groups) if (g.id == member) { found = &g; break; }
        if (!found || found->id == found->parent) break;
        member = found->parent;
    }
    return false;
}
std::vector<std::string> SettingsCatalog::options(const MachineSetting &s, unsigned axes) {
    if (s.type == 4 && (s.format.empty() || s.format == "axes" || s.format.find_first_not_of("0123456789") == std::string::npos)) {
        unsigned n = s.format.empty() || s.format == "axes" ? axes : strtoul(s.format.c_str(), nullptr, 10);
        std::vector<std::string> out;
        for (unsigned i = 0; i < std::min(n, 8u); i++) out.emplace_back(1, "XYZABCUV"[i]);
        return out;
    }
    return s.format.empty() ? std::vector<std::string>{} : split(s.format, ',');
}
bool SettingsCatalog::validate(const MachineSetting &s, const std::string &value, std::string &error) {
    error.clear();
    auto fail = [&](const char *why) { error = why; return false; };
    if (!s.metadata || !s.writable || !s.has_value) return fail("Controller metadata is required for editing");
    if (value.size() > MaxSettingValue) return fail("Value is too long");
    for (unsigned char c : value) if (c < 32 || c > 126) return fail("Use printable text without control characters");
    if (s.type <= 6) {
        double n, bound;
        if (!number(value, n)) return fail("Enter a finite number");
        if (s.type != 6 && (n != floor(n) || n < 0 || n > 4294967295.0)) return fail("Enter a whole non-negative number");
        if (s.type == 0 && n != 0 && n != 1) return fail("Choose On or Off");
        if (!(s.allow_null && n == 0)) {
            if (number(s.minimum, bound) && n < bound) return fail("Value is below the controller's minimum");
            if (number(s.maximum, bound) && n > bound) return fail("Value exceeds the controller's maximum");
        }
        if ((s.type == 1 || s.type == 2 || s.type == 3 || s.type == 4) && !s.format.empty()) {
            auto opts = options(s, 8);
            if (s.type == 3) {
                if (n >= opts.size() || opts[size_t(n)].empty() || opts[size_t(n)] == "N/A") return fail("Choose an available option");
            } else if (opts.size() < 32 && n >= double(uint64_t(1) << opts.size())) return fail("Mask contains unsupported bits");
        }
    } else if (s.type == 9) {
        if (!ip(value)) return fail("Enter four IP address numbers from 0 to 255");
    } else if (s.type == 7 || s.type == 8) {
        double bound;
        if (number(s.minimum, bound) && value.size() < bound) return fail("Text is shorter than the minimum length");
        if (number(s.maximum, bound) && value.size() > bound) return fail("Text exceeds the maximum length");
    } else return fail("Unsupported setting type");
    return true;
}
bool SettingsCatalog::matches(const MachineSetting &s, const std::string &requested) {
    if (!s.has_value) return false;
    if (s.type == 8) return true; // The controller masks password readback; its ACK is required separately.
    if (s.type <= 6) {
        double a, b;
        if (!number(s.value, a) || !number(requested, b)) return false;
        double tolerance = 0;
        size_t dot = s.format.find('.');
        if (s.type == 6) tolerance = dot == std::string::npos ? .000001 : .50001 * pow(10.0, -double(s.format.size() - dot - 1));
        return fabs(a - b) <= tolerance;
    }
    if (s.type == 9 && ip(s.value) && ip(requested)) {
        auto a = split(s.value, '.'), b = split(requested, '.');
        for (size_t i = 0; i < 4; i++) if (atoi(a[i].c_str()) != atoi(b[i].c_str())) return false;
        return true;
    }
    return s.value == requested;
}
}
