#pragma once
#include <stddef.h>
#include <stdint.h>

namespace panel {
constexpr int MaxFiles = 64;
struct File {
    char path[160];
    uint32_t size;
    bool directory;
};
struct ProbeSettings {
    float x = 10, y = 10, z = 5, diameter = 0, feed = 100, inset = 22.5f;
    int corner = 0;
};
struct State {
    bool online = false, identified = false, extended = false, status = false;
    bool coordinates = false, wco_known = false, modal_known = false, inches = false, absolute = true;
    bool report_inches = false, probe = false, probe_known = false, contact_ready = false;
    bool busy = false, probing = false, listing = false, sd_active = false, job_seen_run = false;
    int alarm = 0, error = 0, wcs = 54, line = 0, file_count = 0, probe_stage = 0;
    uint32_t build = 0, last_status = 0, job_started = 0, elapsed = 0, file_revision = 0;
    float machine[3] = {}, work[3] = {}, wco[3] = {}, feed = 0, spindle = 0, percent = 0;
    char mode[20] = "Disconnected", message[192] = "Connect a grblHAL controller", filename[160] = "";
    File files[MaxFiles] = {};
};
class Model {
  public:
    using Line = bool (*)(void *, const char *);
    using Realtime = bool (*)(void *, uint8_t);
    Model(Line line, Realtime realtime, void *context);
    State state;
    void begin(uint32_t now);
    void disconnect();
    void receive(const char *line, uint32_t now);
    void tick(uint32_t now);
    bool idle() const;
    bool list();
    bool run(const char *path);
    bool jog(char axis, float distance, float feed);
    bool jog_vector(const char *directions, float distance, float feed);
    bool zero(int axes);
    bool select_wcs(int wcs);
    bool home(char axis = 0);
    bool unlock();
    bool pause();
    bool resume();
    bool stop();
    bool jog_cancel();
    bool contact_test(uint32_t now);
    bool probe_corner(const ProbeSettings &settings);
    bool acknowledge_error();

  private:
    Line line_;
    Realtime realtime_;
    void *context_;
    char queue_[40][192] = {};
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
