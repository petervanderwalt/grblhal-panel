#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace panel {
constexpr size_t MaxSettingValue = 512;
struct SettingGroup { int id = 0, parent = 0; std::string name; };
struct MachineSetting {
    int id = 0, group = 0, type = 7;
    std::string name, unit, format, minimum, maximum, value, description;
    bool metadata = false, has_value = false, reboot = false, allow_null = false, writable = true, oversized_value = false;
};
class SettingsCatalog {
public:
    std::vector<SettingGroup> groups;
    std::vector<MachineSetting> entries;
    bool overflow = false;
    void clear();
    bool receive(const char *line);
    const MachineSetting *find(int id) const;
    bool in_group(int member, int selected) const;
    static std::vector<std::string> options(const MachineSetting &s, unsigned axes);
    static bool validate(const MachineSetting &s, const std::string &value, std::string &error);
    static bool matches(const MachineSetting &s, const std::string &requested);
private:
    MachineSetting *entry(int id);
};
bool settings_self_test();
}
