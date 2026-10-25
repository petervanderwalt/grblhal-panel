#include "panel_runtime.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gcode_viewer.h"
#include "panel_debug.h"
#include "panel_upload.h"
#include "panel_storage.h"
#include "panel_settings_demo.h"
#include <assert.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace panel {
static SemaphoreHandle_t mutex;
static QueueHandle_t uart_events;
static bool installed = false, demo = false, pending = false;
static unsigned pending_count = 0;
static unsigned demo_rotary_mask = 0;
static char last_tx[640], pending_line[640];
static size_t rx_used = 0;
static char *preview_data = nullptr;
static size_t preview_size = 0;
static bool preview_locked = false;
static bool preview_write(void *, const char *data, size_t size) {
    if (!preview_data || preview_size + size > MaxPreviewBytes)
        return false;
    memcpy(preview_data + preview_size, data, size);
    preview_size += size;
    preview_data[preview_size] = 0;
    return true;
}
static bool rx_overflow = false;
static uint32_t now() { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static bool send_line(void *, const char *s) {
    const char *equal = strchr(s, '=');
    bool setting = s[0] == '$' && equal && equal > s + 1;
    if (setting) for (const char *p = s + 1; p < equal; p++) if (*p < '0' || *p > '9') setting = false;
    if (setting)
        snprintf(last_tx, sizeof last_tx, "%.*s=<hidden>", int(equal - s), s);
    else
        snprintf(last_tx, sizeof last_tx, "%s", s);
    if (demo) {
        snprintf(pending_line, sizeof pending_line, "%s", s);
        pending = true; pending_count++;
        return true;
    }
    if (!installed)
        return false;
    char command[644];
    int n = snprintf(command, sizeof command, "%s\n", s);
    return uart_write_bytes(UART_NUM_1, command, n) == n;
}
extern "C" bool panel_debug_test() {
    bool ok = panel_memory_test();
    ok = panel::model_self_test() && ok;
    ok = panel::stream_self_test() && ok;
    ok = panel::settings_self_test() && ok;
    ok = panel::tools_self_test() && ok;
    ok = panel::actuator_self_test() && ok;
    ok = upload_self_test() && ok;
    bool viewer = gcode_viewer_parser_test();
    printf("@UGTEST %s viewer_parser\n", viewer ? "PASS" : "FAIL");
    return ok && viewer;
}
extern "C" void panel_debug_demo(bool on) { panel::runtime_demo(on); }
extern "C" bool panel_debug_receive(const char *line) { return panel::runtime_inject(line); }
extern "C" void panel_debug_last(char *b, size_t n) { panel::runtime_last_tx(b, n); }
extern "C" void panel_debug_state(char *b, size_t n) {
    static panel::State s;
    panel::runtime_snapshot(s);
    int active_spindle = -1;
    for (unsigned i = 0; i < s.spindle_count; ++i) if (s.spindles[i].active) active_spindle = s.spindles[i].number;
    snprintf(
        b, n,
        "{\"reset_reason\":%d,\"online\":%d,\"identified\":%d,\"extended\":%d,\"ready\":%d,\"alarm\":%d,\"error\":%d,\"x\":%.3f,"
        "\"y\":%.3f,\"z\":%.3f,\"percent\":%.2f,\"job\":%d,\"files\":%d,\"probing\":%d,\"contact\":%d,\"contact_testing\":%d,\"probe_axis\":%d,"
        "\"downloading\":%d,\"download_ready\":%d,\"download_bytes\":%lu,\"axes\":%u,\"homed\":%u,\"a\":%.3f,\"repeat_axis\":%d,\"rotary_known\":%d,\"rotary_mask\":%u,\"settings_loading\":%d,\"settings_saving\":%d,\"settings_count\":%d,\"settings_groups\":%d,\"settings_result\":%d,\"inputs\":%lu,\"inputs_known\":%lu,\"spindle_direction\":%d,\"rpm\":%.0f,\"coolant\":%u,\"macro_running\":%d,\"active_spindle\":%d,\"spindle_count\":%u,\"local_job\":%d,\"checking\":%d,\"job_bytes\":%lu,\"job_total\":%lu,\"file_total\":%u,\"file_offset\":%u}",
        (int)esp_reset_reason(), s.online, s.identified, s.extended, s.status, s.alarm, s.error, s.work[0], s.work[1], s.work[2],
        s.percent, s.sd_active, s.file_count, s.probing, s.contact_ready, s.contact_testing, s.probe_axis, s.downloading, s.download_ready,
        (unsigned long)s.download_bytes, s.axis_count, s.homed_axes, s.machine[3], s.cycle_axis, s.rotary_known, s.rotary_axes >> 3,
        s.settings_loading, s.settings_saving, s.settings_count, s.settings_groups, s.settings_result,
        (unsigned long)s.inputs, (unsigned long)s.inputs_known, s.spindle_direction, s.spindle, s.coolant, s.macro_running, active_spindle, s.spindle_count,s.local_job,s.job_preparing,(unsigned long)s.job_bytes,(unsigned long)s.job_total,s.file_total,s.file_offset);
}
static bool send_rt(void *, uint8_t b) {
    if (b != 0x80 && b != 0x87 && b != '?')
        snprintf(last_tx, sizeof last_tx, "0x%02X", b);
    if (demo)
        return true;
    return installed && uart_write_bytes(UART_NUM_1, &b, 1) == 1;
}
static Model *model_instance;
#define model (*model_instance)
static char *upload_data;
static uint32_t upload_revision;
static bool relist;
static bool send_raw(void *, const uint8_t *bytes, size_t size) {
    return installed && !demo && uart_write_bytes(UART_NUM_1, bytes, size) == (int)size;
}
static Upload upload(send_raw, nullptr);
static void finish_upload() {
    if (!upload_data)
        return;
    model.state.upload_success = upload.success();
    model.state.upload_bytes = upload.transferred();
    model.state.upload_revision = ++upload_revision;
    snprintf(model.state.message, sizeof model.state.message, "%s", upload.message());
    model.transfer_finished(now());
    rx_used = 0;
    rx_overflow = false;
    relist = upload.success();
    free(upload_data);
    upload_data = nullptr;
}
static void cancel_upload(const char *reason) {
    if (upload_data) {
        upload.cancel(reason);
        finish_upload();
    }
}
static SettingsCatalog demo_settings;
static void demo_settings_init() {
    if (!demo_settings.entries.empty()) return;
    for (const char *line : demo_setting_reports) demo_settings.receive(line);
    const char *names[] = {"Travel resolution", "Maximum rate", "Acceleration", "Maximum travel"};
    const char *units[] = {"steps/mm", "mm/min", "mm/s^2", "mm"};
    const char *values[] = {"250.000", "3000.000", "100.000", "500.000"};
    for (int axis = 0; axis < 4; axis++) for (int kind = 0; kind < 4; kind++) {
        char line[256]; int id = 100 + kind * 10 + axis;
        snprintf(line, sizeof line, "[SETTING:%d|%d|%c %s|%s|6|0.000|0.001|100000|0|0]",
                 id, 45 + axis, "XYZA"[axis], names[kind], units[kind]);
        demo_settings.receive(line);
        snprintf(line, sizeof line, "$%d=%s", id, values[kind]); demo_settings.receive(line);
    }
}
static int demo_selected_spindle = 0;
static void mock_reply() {
    if (!pending) {
        return;
    }
    pending = false; pending_count=0;
    demo_settings_init();
    uint32_t t = now();
    if (!strcmp(pending_line, "$I")) {
        model.receive("[VER:1.1f.20261020:]", t);
        model.receive("[FIRMWARE:grblHAL]", t);
        model.receive("<Idle|MPos:125.000,84.500,-12.000|WCO:100,50,-20|FS:0,0|Pn:i>", t);
    } else if (!strcmp(pending_line, "$SPINDLESH")) {
        char report[128];
        snprintf(report, sizeof report, "[SPINDLE:0|0|0|V%s|PWM|0,24000]", demo_selected_spindle == 0 ? "*" : ""); model.receive(report, t);
        snprintf(report, sizeof report, "[SPINDLE:1|1|2|VD%s|RS485 VFD|1000,18000]", demo_selected_spindle == 1 ? "*" : ""); model.receive(report, t);
        model.receive("[SPINDLE:2|-|0|V|PWM2|0,1000]", t);
    } else if (!strncmp(pending_line, "M104Q", 5)) {
        demo_selected_spindle = atoi(pending_line + 5);
    } else if (!strncmp(pending_line, "$376=", 5)) {
        demo_rotary_mask = strtoul(pending_line + 5, nullptr, 10);
        demo_settings.receive(pending_line);
    } else if (!strcmp(pending_line, "$EG")) {
        for (const auto &g : demo_settings.groups) {
            char report[256]; snprintf(report, sizeof report, "[SETTINGGROUP:%d|%d|%s]", g.id, g.parent, g.name.c_str());
            model.receive(report, t);
        }
    } else if (!strcmp(pending_line, "$ES") || !strcmp(pending_line, "$ESH")) {
        for (const auto &s : demo_settings.entries) {
            static char report[3072];
            std::string unit = s.unit;
            if (demo_rotary_mask & 1) {
                if (s.id == 103) unit = "steps/deg";
                if (s.id == 113) unit = "deg/min";
                if (s.id == 123) unit = "deg/s^2";
                if (s.id == 133) unit = "deg";
            }
            if (!strcmp(pending_line, "$ES"))
                snprintf(report, sizeof report, "[SETTING:%d|%d|%s|%s|%d|%s|%s|%s|%d|%d]", s.id, s.group,
                         s.name.c_str(), unit.c_str(), s.type, s.format.c_str(), s.minimum.c_str(), s.maximum.c_str(), s.reboot, s.allow_null);
            else
                snprintf(report, sizeof report, "%d\t%s\t%s\t%d\t%s\t%s\t%s\t%s", s.id, s.name.c_str(), unit.c_str(), s.type,
                         s.format.c_str(), s.description.c_str(), s.minimum.c_str(), s.maximum.c_str());
            model.receive(report, t);
        }
    } else if (!strcmp(pending_line, "$$")) {
        char report[256], positions[96] = {}, offsets[96] = {};
        model.receive("$30=24000", t); model.receive("$31=0", t);
        for (const auto &s : demo_settings.entries) {
            if (s.id == 376) continue;
            snprintf(report, sizeof report, "$%d=%s", s.id, s.value.c_str()); model.receive(report, t);
        }
        snprintf(report, sizeof report, "$376=%u", demo_rotary_mask);
        model.receive(report, t);
        for (unsigned i = 0; i < model.state.axis_count && i < 4; i++) {
            float scale = model.state.report_inches && !(model.state.rotary_axes & (1u << i)) ? 25.4f : 1;
            snprintf(positions + strlen(positions), sizeof positions - strlen(positions), "%s%.3f", i ? "," : "", model.state.machine[i] / scale);
            snprintf(offsets + strlen(offsets), sizeof offsets - strlen(offsets), "%s%.3f", i ? "," : "", model.state.wco[i] / scale);
        }
        snprintf(report, sizeof report, "<%s|MPos:%s|WCO:%s|H:0,%u|FS:0,0>",
                 model.state.mode, positions, offsets, model.state.homed_axes);
        model.receive(report, t);
    } else if (!strcmp(pending_line, "$G")) {
        model.receive("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F100 S0]", t);
    } else if (!strcmp(pending_line, "$F+")) {
        model.receive("[FILE:/bracket.nc|SIZE:24861]", t);
        model.receive("[FILE:/spoilboard.nc|SIZE:98110]", t);
        model.receive("[FILE:/jobs/pocket.nc|SIZE:12480]", t);
    } else if (!strncmp(pending_line, "$F<=", 4)) {
        const char *lines[] = {"(Preview sample)",
                               "G21 G90 G17",
                               "G0 X0 Y0 Z5",
                               "G1 Z-2 F100",
                               "G1 X60 Y0 F600",
                               "Y40",
                               "X0",
                               "Y0",
                               "G0 Z5",
                               "X10 Y10",
                               "G1 Z-2",
                               "X50",
                               "Y30",
                               "X10",
                               "Y10",
                               "G0 Z5",
                               "M30"};
        for (const char *line : lines)
            model.receive(line, t);
        model.receive("ok", t);
        model.receive("<Idle|FS:0,0>", t);
        return;
    } else if (!strncmp(pending_line, "$F=", 3)) {
        char line[256];
        snprintf(line, sizeof line, "<Run|MPos:125,84.5,-12|FS:800,12000|SD:0,%s>", pending_line + 3);
        model.receive(line, t);
    } else if (pending_line[0] == '$' && pending_line[1] >= '0' && pending_line[1] <= '9' && strchr(pending_line, '=')) {
        demo_settings.receive(pending_line);
    }
    // Probe reports are injected separately; an ok alone must never advance a probe.
    model.receive("ok", t);
    if (pending_line[0] == 'M') {
        int m = atoi(pending_line + 1);
        int direction = model.state.spindle_direction;
        float rpm = model.state.spindle;
        unsigned coolant = model.state.coolant;
        if (m == 3 || m == 4) { direction = m == 3 ? 1 : -1; const char *s = strchr(pending_line, 'S'); if (s) rpm = strtof(s + 1, nullptr); }
        if (m == 5) { direction = 0; rpm = 0; }
        if (m == 7) coolant |= 1;
        if (m == 8) coolant |= 2;
        if (m == 9) coolant = 0;
        char feedback[96]; snprintf(feedback, sizeof feedback, "<Idle|FS:0,%.0f|A:%s%s%s>", rpm,
            direction == 1 ? "S" : direction == -1 ? "C" : "", coolant & 1 ? "M" : "", coolant & 2 ? "F" : "");
        model.receive(feedback, t);
    }
    if (!strcmp(pending_line, "$G") && !model.state.sd_active && !model.state.probing && !model.state.macro_running)
        model.receive("<Idle|FS:0,0>", t);
}
static void worker(void *) {
    static char line[4096];
    size_t &used = rx_used;
    bool &overflow = rx_overflow;
    uint32_t demo_status = 0;
    for (;;) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (installed && !demo) {
            uart_event_t event;
            while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
                if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                    cancel_upload("UART overflow during SD upload");
                    model.disconnect();
                    uart_flush_input(UART_NUM_1);
                    used = 0;
                    overflow = false;
                    snprintf(model.state.message, sizeof model.state.message,
                             "UART overflow; preview invalid. Reconnect controller.");
                }
            }
            uint8_t bytes[1024];
            for (int batch = 0; batch < 8; batch++) {
                int n = uart_read_bytes(UART_NUM_1, bytes, sizeof bytes, 0);
                if (!n)
                    break;
                for (int i = 0; i < n; i++) {
                    uint8_t c = bytes[i];
                    if (model.state.uploading) {
                        upload.receive(c, now());
                        continue;
                    }
                    if (c == '\r' || c == '\n') {
                        if (overflow && model.state.downloading) {
                            model.disconnect();
                            snprintf(model.state.message, sizeof model.state.message,
                                     "Preview line too long; reconnect controller");
                        } else if (overflow && (model.state.settings_loading || model.state.settings_saving)) {
                            model.disconnect();
                            snprintf(model.state.settings_message, sizeof model.state.settings_message,
                                     "Controller metadata exceeded the line limit; reconnect");
                        } else if (used && !overflow) {
                            line[used] = 0;
                            model.receive(line, now());
                        }
                        used = 0;
                        overflow = false;
                    } else if ((c >= 32 && c < 127) || c == '\t') {
                        if (used + 1 < sizeof line && !overflow)
                            line[used++] = c;
                        else
                            overflow = true;
                    }
                }
            }
        }
        uint32_t t = now();
        if (demo && model.state.identified && t - demo_status >= 250) {
            // Keep preview feedback alive. Tests can inject positions, SD progress and alarms.
            model.state.last_status = t;
            demo_status = t;
        }
        if (upload_data) {
            upload.tick(t);
            model.state.upload_bytes = upload.transferred();
            if (!upload.active())
                finish_upload();
        }
        if (!model.state.uploading)
            model.tick(t);
        if (relist && model.idle()) {
            relist = false;
            model.list();
        }
        if (demo && model.state.local_job) {
            if(pending) { unsigned n=pending_count;pending=false;pending_count=0;
                for(unsigned i=0;i<n;i++)model.receive("ok",t);
                if(!strncmp(model.state.mode,"Idle",4)||!strncmp(model.state.mode,"Run",3))model.receive("<Run|FS:800,12000>",t);
            } else if(model.local_stream_done()&&strncmp(model.state.mode,"Hold",4))model.receive("<Idle|FS:0,0>",t);
        } else if (demo) mock_reply();
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(model.state.local_job ? 1 : 20));
    }
}
void runtime_init() {
    if (mutex)
        return;
    mutex = xSemaphoreCreateMutex();
    assert(mutex);
    void *memory=heap_caps_malloc(sizeof(Model),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    assert(memory);model_instance=new(memory) Model(send_line,send_rt,nullptr);
    storage_init();
    BaseType_t ok = xTaskCreate(worker, "panel_link", 8192, nullptr, 5, nullptr);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
bool runtime_connect(int tx, int rx, int baud) {
    if (tx < 0 || tx > 54 || rx < 0 || rx > 54 || tx == rx || baud < 9600 || baud > 921600)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    cancel_upload("SD upload cancelled by reconnect");
    if (installed) {
        uart_driver_delete(UART_NUM_1);
        installed = false;
    }
    demo = false;
    pending = false;
    rx_used = 0;
    rx_overflow = false;
    model.disconnect();
    uart_config_t c = {};
    c.baud_rate = baud;
    c.data_bits = UART_DATA_8_BITS;
    c.parity = UART_PARITY_DISABLE;
    c.stop_bits = UART_STOP_BITS_1;
    c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    c.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(UART_NUM_1, 16384, 0, 20, &uart_events, 0);
    if (err == ESP_OK) {
        installed = true;
        err = uart_param_config(UART_NUM_1, &c);
        if (err == ESP_OK)
            err = uart_set_pin(UART_NUM_1, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        uart_flush_input(UART_NUM_1);
        model.begin(now());
    } else {
        if (installed)
            uart_driver_delete(UART_NUM_1);
        installed = false;
    }
    xSemaphoreGive(mutex);
    return err == ESP_OK;
}
void runtime_disconnect() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    cancel_upload("SD upload disconnected");
    if (installed)
        uart_driver_delete(UART_NUM_1);
    installed = false;
    rx_used = 0;
    rx_overflow = false;
    demo = false;
    pending = false;
    model.disconnect();
    xSemaphoreGive(mutex);
}
bool runtime_background_idle() {
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ok=!model.state.sd_active&&!model.state.busy&&!model.state.probing&&!model.state.uploading&&
        !model.state.alarm&&!model.state.error&&(!model.state.online||!strcmp(model.state.mode,"Idle"));
    xSemaphoreGive(mutex);return ok;
}
void runtime_snapshot(State &s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    // Avoid ROM PSRAM-to-PSRAM copies on this P4 revision, as for framebuffer staging.
    auto dst=(volatile uint8_t *)&s;auto src=(const volatile uint8_t *)&model.state;
    for(size_t i=0;i<sizeof s;i++)dst[i]=src[i];
    if(storage_source()) storage_files(s);
    xSemaphoreGive(mutex);
}
bool runtime_action(Action a, const char *text, float x, float y) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = false;
    switch (a) {
    case Action::FeedOverride:
    case Action::SpindleOverride:
        ok = model.override_target(a == Action::SpindleOverride, (int)x);
        break;
    case Action::JobAcknowledged:
        ok = !model.state.sd_active;
        if (ok)
            model.state.job_complete = false;
        break;
    case Action::List:
        if(storage_source()){storage_scan();ok=true;} else ok = model.list();
        break;
    case Action::Run:
        if(storage_source()) {
            uint32_t size=0;FILE *file=storage_open(text,&size);
            ok=file&&model.run_local(file,text,size,now());
            if(file&&!ok)fclose(file);
        } else ok = model.run(text);
        break;
    case Action::Preview:
        if (storage_source()) { storage_prioritize(text); ok=true; break; }
        if (!preview_locked && model.idle()) {
            if (!preview_data)
                preview_data =
                    (char *)heap_caps_malloc(MaxPreviewBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (preview_data) {
                ok = model.download(text, preview_write, nullptr);
                if (ok)
                    preview_size = 0;
            }
        }
        break;
    case Action::Jog:
        ok = model.jog_preset(text, x, (int)y);
        break;
    case Action::Spindle:
        ok = model.spindle_control((int)x, y);
        break;
    case Action::SpindleList:
        ok = model.spindle_list();
        break;
    case Action::SpindleSelect:
        ok = model.spindle_select((int)x);
        break;
    case Action::Coolant:
        ok = model.coolant_control((unsigned)x);
        break;
    case Action::Macro:
        ok = model.macro_run(text);
        break;
    case Action::Zero:
        ok = model.zero((int)x);
        break;
    case Action::GotoZero:
        ok = model.goto_zero((int)x);
        break;
    case Action::Wcs:
        ok = model.select_wcs((int)x);
        break;
    case Action::Home:
        ok = model.home(text ? text[0] : 0);
        break;
    case Action::ActuatorHome:
        ok = text && model.actuator_home(text[0]);
        break;
    case Action::Unlock:
        ok = model.unlock();
        break;
    case Action::Pause:
        ok = model.pause();
        break;
    case Action::Resume:
        ok = model.resume();
        break;
    case Action::Stop:
        cancel_upload("SD upload stopped");
        ok = model.stop();
        break;
    case Action::JogCancel:
        ok = model.jog_cancel();
        break;
    case Action::Contact:
        ok = model.contact_test(now());
        break;
    case Action::Acknowledge:
        ok = model.acknowledge_error();
        break;
    }
    if (demo && ok) {
        uint32_t t = now();
        if (a == Action::FeedOverride || a == Action::SpindleOverride) {
            char report[80];
            snprintf(report, sizeof report, "<%s|Ov:%d,100,%d>", model.state.mode,
                     a == Action::FeedOverride ? (int)x : model.state.feed_override,
                     a == Action::SpindleOverride ? (int)x : model.state.spindle_override);
            model.receive(report, t);
        }
        if (a == Action::JogCancel)
            model.receive("<Idle|FS:0,0>", t);
        if (a == Action::Pause)
            model.receive("<Hold:0|FS:0,12000>", t);
        if (a == Action::Resume)
            model.receive("<Run|FS:800,12000>", t);
        if (a == Action::Stop) {
            model.begin(t);
        }
    }
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_settings_load() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.settings_load();
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_setting_write(int id, const char *value) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.setting_write(id, value);
    xSemaphoreGive(mutex);
    return ok;
}
void runtime_settings_snapshot(SettingsCatalog &catalog) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    catalog = model.settings;
    xSemaphoreGive(mutex);
}
bool runtime_actuator_type(bool rotary) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.actuator_type(rotary);
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_actuator_move(int axis, float target, float feed, float end1, float end2, bool rotary) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.actuator_move(axis, target, feed, end1, end2, rotary);
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_actuator_cycle(int axis, float feed, float end1, float end2, bool rotary) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.actuator_cycle(axis, feed, end1, end2, rotary);
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_actuator_cycle_speed(float feed) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.actuator_cycle_speed(feed);
    if (demo && ok)
        model.receive("<Idle|FS:0,0>", now()); // Position-bearing test feedback completes the restart.
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_probe(const ProbeSettings &s, int axis, int direction) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = axis < 0 ? model.probe_corner(s) : model.probe_surface(s, axis, direction);
    xSemaphoreGive(mutex);
    return ok;
}
void runtime_demo(bool on) {
    runtime_disconnect();
    xSemaphoreTake(mutex, portMAX_DELAY);
    demo = on;
    if (on)
        model.begin(now());
    xSemaphoreGive(mutex);
}
bool runtime_is_demo() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool on = demo;
    xSemaphoreGive(mutex);
    return on;
}
bool runtime_inject(const char *s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = demo;
    if (ok) {
        bool settings_line = !model.state.downloading && (s[0] == '$' || !strncmp(s, "[SETTING", 8));
        model.receive(s, now());
        if (settings_line) { demo_settings_init(); demo_settings.receive(s); }
        if (!strncmp(s, "$376=", 5) && model.state.rotary_known)
            demo_rotary_mask = model.state.rotary_axes >> 3;
    }
    xSemaphoreGive(mutex);
    return ok;
}
void runtime_last_tx(char *s, size_t n) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(s, n, "%s", last_tx);
    xSemaphoreGive(mutex);
}
bool runtime_preview_acquire(const char **data, size_t *size) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.state.download_ready && !model.state.downloading && !preview_locked;
    if (ok) {
        preview_locked = true;
        *data = preview_data;
        *size = preview_size;
    }
    xSemaphoreGive(mutex);
    return ok;
}
void runtime_preview_release() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    preview_locked = false;
    xSemaphoreGive(mutex);
}
bool runtime_can_upload_local() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ready = !model.state.busy && !model.state.sd_active && !model.state.probing &&
        !model.state.uploading && (!model.state.online || model.idle()) && storage_can_upload();
    xSemaphoreGive(mutex);
    return ready;
}
bool runtime_upload_local(const char *path, const char *data, size_t size) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ready = !model.state.busy && !model.state.sd_active && !model.state.probing &&
        !model.state.uploading && (!model.state.online || model.idle());
    bool ok = ready && storage_upload(path, data, size);
    if (ready) {
        model.state.upload_success = ok;
        model.state.upload_bytes = ok ? size : 0;
        model.state.upload_total = size;
        model.state.upload_revision = ++upload_revision;
        snprintf(model.state.upload_path, sizeof model.state.upload_path, "%s", path);
        snprintf(model.state.message, sizeof model.state.message, "%s",
                 ok ? "Saved to display SD" : "Save failed: existing file, full or unavailable SD");
    }
    xSemaphoreGive(mutex);
    return ok;
}
bool runtime_can_upload() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ready = installed && !demo && model.idle() && model.state.ymodem && !preview_locked && !upload_data;
    xSemaphoreGive(mutex);
    return ready;
}
bool runtime_upload(const char *path, const char *data, size_t size) {
    if (!path || !data || !size || size > MaxPreviewBytes)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ready = installed && !demo && model.idle() && model.state.ymodem && !preview_locked && !upload_data;
    if (ready) {
        upload_data = (char *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ready = upload_data != nullptr;
        if (ready) {
            memcpy(upload_data, data, size);
            model.state.uploading = model.state.busy = true;
            model.state.upload_success = false;
            model.state.upload_bytes = 0;
            model.state.upload_total = size;
            model.state.upload_revision = ++upload_revision;
            snprintf(model.state.upload_path, sizeof model.state.upload_path, "%s", path);
            rx_used = 0;
            rx_overflow = false;
            uart_flush_input(UART_NUM_1);
            ready = upload.start(path, upload_data, size, now());
            if (!ready)
                finish_upload();
        }
    }
    xSemaphoreGive(mutex);
    return ready;
}
void runtime_cancel_upload() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    cancel_upload("SD upload cancelled");
    xSemaphoreGive(mutex);
}
} // namespace panel
