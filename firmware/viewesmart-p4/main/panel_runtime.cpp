#include "panel_runtime.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gcode_viewer.h"
#include "panel_debug.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace panel {
static SemaphoreHandle_t mutex;
static QueueHandle_t uart_events;
static bool installed = false, demo = false, pending = false;
static char last_tx[192], pending_line[192];
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
    snprintf(last_tx, sizeof last_tx, "%s", s);
    if (demo) {
        snprintf(pending_line, sizeof pending_line, "%s", s);
        pending = true;
        return true;
    }
    if (!installed)
        return false;
    char command[196];
    int n = snprintf(command, sizeof command, "%s\n", s);
    return uart_write_bytes(UART_NUM_1, command, n) == n;
}
extern "C" bool panel_debug_test() {
    bool ok = panel::model_self_test();
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
    snprintf(
        b, n,
        "{\"online\":%d,\"identified\":%d,\"extended\":%d,\"ready\":%d,\"alarm\":%d,\"error\":%d,\"x\":%.3f,"
        "\"y\":%.3f,\"z\":%.3f,\"percent\":%.2f,\"job\":%d,\"files\":%d,\"probing\":%d,\"contact\":%d,"
        "\"downloading\":%d,\"download_ready\":%d,\"download_bytes\":%lu}",
        s.online, s.identified, s.extended, s.status, s.alarm, s.error, s.work[0], s.work[1], s.work[2],
        s.percent, s.sd_active, s.file_count, s.probing, s.contact_ready, s.downloading, s.download_ready,
        (unsigned long)s.download_bytes);
}
static bool send_rt(void *, uint8_t b) {
    if (b != 0x80 && b != 0x87 && b != '?')
        snprintf(last_tx, sizeof last_tx, "0x%02X", b);
    if (demo)
        return true;
    return installed && uart_write_bytes(UART_NUM_1, &b, 1) == 1;
}
static Model model(send_line, send_rt, nullptr);
static void mock_reply() {
    if (!pending) {
        return;
    }
    pending = false;
    uint32_t t = now();
    if (!strcmp(pending_line, "$I")) {
        model.receive("[VER:1.1f.20261020:]", t);
        model.receive("[FIRMWARE:grblHAL]", t);
        model.receive("<Idle|MPos:125.000,84.500,-12.000|WCO:100,50,-20|FS:0,0|Pn:i>", t);
    } else if (!strcmp(pending_line, "$G"))
        model.receive("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F100 S0]", t);
    else if (!strcmp(pending_line, "$F+")) {
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
    }
    // Probe reports are injected separately; an ok alone must never advance a probe.
    model.receive("ok", t);
}
static void worker(void *) {
    char line[768];
    size_t &used = rx_used;
    bool &overflow = rx_overflow;
    uint32_t demo_status = 0;
    for (;;) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (installed && !demo) {
            uart_event_t event;
            while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
                if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
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
                    if (c == '\r' || c == '\n') {
                        if (overflow && model.state.downloading) {
                            model.disconnect();
                            snprintf(model.state.message, sizeof model.state.message,
                                     "Preview line too long; reconnect controller");
                        } else if (used && !overflow) {
                            line[used] = 0;
                            model.receive(line, now());
                        }
                        used = 0;
                        overflow = false;
                    } else if (c >= 32 && c < 127) {
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
        model.tick(t);
        if (demo)
            mock_reply();
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void runtime_init() {
    if (mutex)
        return;
    mutex = xSemaphoreCreateMutex();
    assert(mutex);
    BaseType_t ok = xTaskCreate(worker, "panel_link", 8192, nullptr, 5, nullptr);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
bool runtime_connect(int tx, int rx, int baud) {
    if (tx < 0 || tx > 54 || rx < 0 || rx > 54 || tx == rx || baud < 9600 || baud > 921600)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
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
void runtime_snapshot(State &s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    s = model.state;
    xSemaphoreGive(mutex);
}
bool runtime_action(Action a, const char *text, float x, float y) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = false;
    switch (a) {
    case Action::List:
        ok = model.list();
        break;
    case Action::Run:
        ok = model.run(text);
        break;
    case Action::Preview:
        if (!preview_locked && model.idle()) {
            if (!preview_data)
                preview_data =
                    (char *)heap_caps_malloc(MaxPreviewBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (preview_data) {
                ok = model.download(text, preview_write, nullptr);
                if (ok) preview_size = 0;
            }
        }
        break;
    case Action::Jog:
        ok = text && (strlen(text) == 3 ? model.jog_vector(text, x, y) : model.jog(text[0], x, y));
        break;
    case Action::Zero:
        ok = model.zero((int)x);
        break;
    case Action::Wcs:
        ok = model.select_wcs((int)x);
        break;
    case Action::Home:
        ok = model.home(text ? text[0] : 0);
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
bool runtime_probe(const ProbeSettings &s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = model.probe_corner(s);
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
    if (ok)
        model.receive(s, now());
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
} // namespace panel
