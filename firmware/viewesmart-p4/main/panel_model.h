#pragma once
#include <stddef.h>
#include <stdint.h>
#include "panel_settings.h"
#include "panel_stream.h"

namespace panel {
constexpr int MaxFiles = 64;
constexpr size_t MaxPreviewBytes = 2 * 1024 * 1024;
inline float display_scale(bool inches) { return inches ? 25.4f : 1.f; }
inline float jog_step_mm(bool inches, unsigned index) {
    const float steps[] = {.1f, 1.f, 10.f, 100.f};
    return index < 4 ? steps[index] * (inches ? 2.54f : 1.f) : 0;
}
struct File {
    char path[160];
    uint32_t size;
    bool directory;
    bool unusable;
};
struct ProbeSettings {
    float x = 10, y = 10, z = 5, diameter = 0, feed = 100, inset = 22.5f;
    int corner = 0;
};
struct Spindle {
    int number = -1;
    char name[64] = "";
    bool active = false, limits_known = false;
    float minimum = 0, maximum = 0;
};
struct State {
    bool local_job = false, job_preparing = false;
    uint32_t job_bytes = 0, job_total = 0;
    unsigned file_total=0,file_offset=0;
    Spindle spindles[8];
    unsigned spindle_count = 0, spindle_revision = 0;
    bool spindles_loading = false;
    int spindles_result = 0;
    bool online = false, identified = false, extended = false, status = false;
    bool ymodem = false;
    bool coordinates = false, wco_known = false, modal_known = false, inches = false, absolute = true;
    bool report_inches = false, report_units_known = false, probe = false, probe_known = false, contact_ready = false, contact_testing = false;
    bool rotary_known = false;
    uint32_t inputs = 0, inputs_known = 0;
    bool pins_reporting_known = false, pins_reporting = false;
    uint8_t spindle_limits_seen = 0;
    bool accessories_known = false, spindle_limits_known = false, macro_running = false;
    bool macro_failed = false;
    int spindle_direction = 0;
    unsigned coolant = 0;
    float spindle_min = 0, spindle_max = 0;
    bool grblhal = false, legacy_rt_known = false, legacy_rt = false;
    bool settings_loading = false, settings_saving = false, settings_ready = false;
    int settings_count = 0, settings_groups = 0, settings_result = 0;
    uint32_t settings_revision = 0;
    char settings_message[128] = "";
    uint8_t rotary_axes = 0;
    bool busy = false, probing = false, listing = false, sd_active = false, job_seen_run = false;
    bool downloading = false, download_ready = false;
    bool uploading = false, upload_success = false;
    uint32_t upload_bytes = 0, upload_total = 0, upload_revision = 0;
    char upload_path[101] = "";
    bool job_complete = false, job_end = false, job_failed = false, overrides_known = false;
    int feed_override = 100, spindle_override = 100;
    uint32_t download_bytes = 0, download_total = 0, download_revision = 0;
    char download_path[160] = "";
    int alarm = 0, error = 0, wcs = 54, line = 0, file_count = 0, probe_stage = 0, probe_axis = -1;
    uint32_t build = 0, last_status = 0, job_started = 0, elapsed = 0, file_revision = 0;
    float machine[4] = {}, work[4] = {}, wco[4] = {}, feed = 0, spindle = 0, percent = 0;
    float axis_max_rate[4] = {};
    unsigned axis_count = 0, wco_axes = 0, homed_axes = 0;
    bool machine_known = false;
    int cycle_axis = -1;
    char mode[20] = "Disconnected", message[192] = "Connect a grblHAL controller", filename[160] = "";
    File files[MaxFiles] = {};
};
class Model {
  public:
    using Line = bool (*)(void *, const char *);
    using Realtime = bool (*)(void *, uint8_t);
    Model(Line line, Realtime realtime, void *context);
    State state;
    SettingsCatalog settings;
    void begin(uint32_t now);
    void disconnect();
    void receive(const char *line, uint32_t now);
    void tick(uint32_t now);
    bool idle() const;
    bool list();
    bool run(const char *path);
    bool local_stream_done() const { return stream_.done(); }
    bool run_local(FILE *file, const char *path, uint32_t size, uint32_t now);
    using Download = bool (*)(void *, const char *, size_t);
    bool download(const char *path, Download sink, void *context);
    bool jog(char axis, float distance, float feed);
    bool jog_vector(const char *directions, float distance, float feed);
    bool jog_preset(const char *directions, float distance, int speed);
    bool zero(int axes);
    bool goto_zero(int axes);
    bool select_wcs(int wcs);
    bool home(char axis = 0);
    bool actuator_home(char axis);
    bool actuator_type(bool rotary);
    bool settings_load();
    bool setting_write(int id, const char *value);
    bool actuator_move(int axis, float target, float feed, float end1, float end2, bool rotary = false);
    bool actuator_cycle(int axis, float feed, float end1, float end2, bool rotary = false);
    bool actuator_cycle_speed(float feed);
    bool unlock();
    bool pause();
    bool resume();
    bool stop();
    bool jog_cancel();
    bool contact_test(uint32_t now);
    bool probe_corner(const ProbeSettings &settings);
    bool probe_surface(const ProbeSettings &settings, int axis, int direction);
    bool acknowledge_error();
    bool override_target(bool spindle, int percent);
    bool spindle_control(int direction, float rpm);
    bool spindle_list();
    bool spindle_select(int number);
    bool coolant_control(unsigned mode);
    bool macro_run(const char *code);
    void transfer_finished(uint32_t now);

  private:
    Line line_;
    Realtime realtime_;
    void *context_;
    Stream stream_;
    bool stream_paused_ = false;
    Download download_sink_ = nullptr;
    void *download_context_ = nullptr;
    bool download_failed_ = false;
    bool goto_pending_ = false;
    bool actuator_pending_ = false;
    float cycle_ends_[2] = {}, cycle_target_ = 0, cycle_feed_ = 0;
    bool cycle_rotary_ = false, cycle_resume_ = false, cycle_speed_pending_ = false;
    std::string queue_[40];
    int setting_write_id_ = -1;
    bool setting_value_seen_ = false, setting_password_ = false, setting_reboot_ = false;
    std::string setting_requested_;
    int queued_ = 0, cursor_ = 0;
    bool awaiting_ = false, need_probe_ = false, got_probe_ = false, initialized_ = false,
         full_report_ = false;
    int contact_stage_ = 0;
    uint32_t sent_ = 0, poll_ = 0, now_ = 0, contact_started_ = 0, identified_at_ = 0;
    bool append(const char *command);
    void cancel(const char *message);
    bool action(const char *command);
    bool rt(uint8_t byte);
    void status_report(const char *line);
};
} // namespace panel
