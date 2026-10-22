#include "cnc_link.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr size_t kRxBufferSize = 4096;
constexpr size_t kLineBufferSize = 160;
constexpr uint32_t kDefaultStatusPeriodMs = 350;
constexpr int kGrblRxMax = 128;
constexpr int kLineQueueSize = 64;
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr uint32_t kStatusTimeoutMs = 2000;
static const char *TAG = "cnc_link";

static int s_chars_pending = 0;
static int s_line_queue[kLineQueueSize];
static int s_queue_head = 0;
static int s_queue_tail = 0;
static int s_queue_count = 0;
static uint32_t s_acked_count = 0;
static SemaphoreHandle_t s_queue_mutex = nullptr;

struct LinkState {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t rx_task = nullptr;
    bool initialized = false;
    bool running = false;
    bool connected = false;
    bool uart_installed = false;
    bool has_status = false;
    bool machine_coordinates = true;
    cnc_link_config_t config = {};
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float a = 0.0f;
    float feed = 0.0f;
    float spindle = 0.0f;
    uint32_t connect_time_ms = 0;
    uint32_t last_status_ms = 0;
    char machine_state[16] = "Offline";
    char last_message[64] = "Idle";
    esp_ldo_channel_handle_t rs485_ldo = nullptr;
};

LinkState s_link;

static bool parse_float_list(const char *text, float *values, size_t max_values, size_t *parsed_count)
{
    size_t count = 0;
    const char *cursor = text;

    while (*cursor && count < max_values) {
        char *end = nullptr;
        values[count] = strtof(cursor, &end);
        if (end == cursor) {
            break;
        }

        count++;
        cursor = (*end == ',') ? end + 1 : end;
        if (*end != ',' && *end != '\0') {
            break;
        }
    }

    if (parsed_count) {
        *parsed_count = count;
    }
    return count > 0;
}

static void lock_state(void)
{
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
}

static void unlock_state(void)
{
    xSemaphoreGive(s_link.mutex);
}

static void update_message_locked(const char *message)
{
    strlcpy(s_link.last_message, message, sizeof(s_link.last_message));
}

static esp_err_t enable_rs485_power(void)
{
    if (s_link.rs485_ldo) {
        return ESP_OK;
    }

    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };

    return esp_ldo_acquire_channel(&ldo_cfg, &s_link.rs485_ldo);
}

static void parse_status_report(const char *line)
{
    char local[kLineBufferSize];
    char *save = nullptr;
    char *token = nullptr;
    bool machine_coordinates = s_link.machine_coordinates;
    float coord[4] = {s_link.x, s_link.y, s_link.z, s_link.a};
    float feed = s_link.feed;
    float spindle = s_link.spindle;

    strlcpy(local, line + 1, sizeof(local));
    char *end = strchr(local, '>');
    if (end) {
        *end = '\0';
    }

    token = strtok_r(local, "|", &save);
    if (!token) {
        return;
    }

    lock_state();
    strlcpy(s_link.machine_state, token, sizeof(s_link.machine_state));

    while ((token = strtok_r(nullptr, "|", &save)) != nullptr) {
        if (strncmp(token, "MPos:", 5) == 0 || strncmp(token, "WPos:", 5) == 0) {
            size_t parsed = 0;
            machine_coordinates = (token[0] == 'M');
            if (parse_float_list(token + 5, coord, 4, &parsed)) {
                if (parsed < 4) {
                    coord[3] = s_link.a;
                }
            }
        } else if (strncmp(token, "FS:", 3) == 0 || strncmp(token, "F:", 2) == 0) {
            float values[2] = {0};
            size_t parsed = 0;
            const char *payload = token[1] == 'S' ? token + 3 : token + 2;
            if (parse_float_list(payload, values, 2, &parsed)) {
                feed = values[0];
                if (parsed > 1) {
                    spindle = values[1];
                }
            }
        }
    }

    s_link.machine_coordinates = machine_coordinates;
    s_link.x = coord[0];
    s_link.y = coord[1];
    s_link.z = coord[2];
    s_link.a = coord[3];
    s_link.feed = feed;
    s_link.spindle = spindle;
    s_link.has_status = true;
    s_link.last_status_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    update_message_locked("Streaming status");
    unlock_state();
}

static void parse_line(const char *line)
{
    if (!line[0]) {
        return;
    }

    if (line[0] == '<') {
        parse_status_report(line);
        return;
    }

    bool is_response = (strcmp(line, "ok") == 0) || (strncmp(line, "error:", 6) == 0);

    if (is_response && s_queue_mutex && xSemaphoreTake(s_queue_mutex, portMAX_DELAY)) {
        if (s_queue_count > 0) {
            int consumed = s_line_queue[s_queue_tail];
            s_queue_tail = (s_queue_tail + 1) % kLineQueueSize;
            s_queue_count--;
            s_chars_pending -= consumed;
            if (s_chars_pending < 0) {
                s_chars_pending = 0;
            }
            s_acked_count++;
        }
        xSemaphoreGive(s_queue_mutex);
    }

    lock_state();
    if (strcmp(line, "ok") == 0) {
        update_message_locked("ok");
    } else if (strncmp(line, "error:", 6) == 0) {
        snprintf(s_link.last_message, sizeof(s_link.last_message), "error %s", line + 6);
    } else if (strncmp(line, "ALARM:", 6) == 0) {
        strlcpy(s_link.machine_state, "Alarm", sizeof(s_link.machine_state));
        snprintf(s_link.last_message, sizeof(s_link.last_message), "alarm %s", line + 6);
    } else if (line[0] == '[') {
        strlcpy(s_link.last_message, line, sizeof(s_link.last_message));
    } else {
        strlcpy(s_link.last_message, line, sizeof(s_link.last_message));
    }
    unlock_state();
}

static void rx_task_main(void *arg)
{
    int uart_num = (int)(intptr_t)arg;
    char line[kLineBufferSize];
    size_t used = 0;
    uint8_t byte = 0;

    while (s_link.running) {
        int len = uart_read_bytes((uart_port_t)uart_num, &byte, 1, pdMS_TO_TICKS(50));
        if (len <= 0) {
            continue;
        }

        if (byte == '\r' || byte == '\n') {
            if (used > 0) {
                line[used] = '\0';
                parse_line(line);
                used = 0;
            }
            continue;
        }

        if (used + 1 < sizeof(line) && isprint(byte)) {
            line[used++] = (char)byte;
        } else if (used + 1 >= sizeof(line)) {
            line[used] = '\0';
            parse_line(line);
            used = 0;
        }
    }

    s_link.rx_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" esp_err_t cnc_link_init(void)
{
    if (s_link.initialized) {
        return ESP_OK;
    }

    s_link.mutex = xSemaphoreCreateMutex();
    if (!s_link.mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_queue_mutex = xSemaphoreCreateMutex();
    if (!s_queue_mutex) {
        vSemaphoreDelete(s_link.mutex);
        s_link.mutex = nullptr;
        return ESP_ERR_NO_MEM;
    }

    cnc_link_get_default_config(&s_link.config);
    s_link.initialized = true;
    return ESP_OK;
}

extern "C" void cnc_link_get_default_config(cnc_link_config_t *config)
{
    if (!config) {
        return;
    }

    config->uart_num = 1;
    config->tx_pin = 27;
    config->rx_pin = 26;
    config->baud_rate = 115200;
    config->use_rs485_power = true;
}

extern "C" esp_err_t cnc_link_connect(const cnc_link_config_t *config)
{
    uart_config_t uart_config = {};
    cnc_link_config_t applied = {};

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->uart_num < 0 || config->uart_num > 5 || config->tx_pin < 0 || config->rx_pin < 0 || config->baud_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(cnc_link_init(), TAG, "init failed");

    if (s_link.connected) {
        cnc_link_disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    applied = *config;
    if (applied.use_rs485_power) {
        esp_err_t ldo_err = enable_rs485_power();
        if (ldo_err != ESP_OK) {
            ESP_LOGW(TAG, "rs485 power not available, continuing");
        }
    }

    uart_config.baud_rate = applied.baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_RETURN_ON_ERROR(uart_driver_install((uart_port_t)applied.uart_num, kRxBufferSize, 0, 0, nullptr, 0), TAG, "uart_driver_install failed");
    s_link.uart_installed = true;
    ESP_RETURN_ON_ERROR(uart_param_config((uart_port_t)applied.uart_num, &uart_config), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin((uart_port_t)applied.uart_num, applied.tx_pin, applied.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin failed");

    lock_state();
    s_link.config = applied;
    s_link.connected = true;
    s_link.running = true;
    s_link.has_status = false;
    s_link.connect_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_link.feed = 0.0f;
    s_link.spindle = 0.0f;
    strlcpy(s_link.machine_state, "Connecting", sizeof(s_link.machine_state));
    snprintf(s_link.last_message, sizeof(s_link.last_message), "UART%d %d %d@%d", applied.uart_num, applied.tx_pin, applied.rx_pin, applied.baud_rate);
    unlock_state();

    if (xTaskCreate(rx_task_main, "cnc_rx", 4096, (void *)(intptr_t)applied.uart_num, 9, &s_link.rx_task) != pdPASS) {
        lock_state();
        s_link.running = false;
        s_link.connected = false;
        s_link.uart_installed = false;
        unlock_state();
        uart_driver_delete((uart_port_t)applied.uart_num);
        return ESP_ERR_NO_MEM;
    }

    uart_flush_input((uart_port_t)applied.uart_num);
    cnc_link_request_status();
    return ESP_OK;
}

extern "C" void cnc_link_disconnect(void)
{
    uart_port_t uart_num;

    if (!s_link.initialized) {
        return;
    }

    lock_state();
    uart_num = (uart_port_t)s_link.config.uart_num;
    s_link.running = false;
    s_link.connected = false;
    s_link.has_status = false;
    strlcpy(s_link.machine_state, "Offline", sizeof(s_link.machine_state));
    update_message_locked("Disconnected");
    unlock_state();

    vTaskDelay(pdMS_TO_TICKS(80));
    if (s_link.rx_task) {
        vTaskDelete(s_link.rx_task);
        s_link.rx_task = nullptr;
    }

    if (s_link.uart_installed) {
        s_link.uart_installed = false;
        uart_driver_delete(uart_num);
    }
}

extern "C" bool cnc_link_is_connected(void)
{
    bool connected;
    uint32_t now;
    uint32_t last_status;
    uint32_t connect_time;

    if (!s_link.initialized) {
        return false;
    }

    lock_state();
    connected = s_link.connected;
    last_status = s_link.last_status_ms;
    connect_time = s_link.connect_time_ms;
    unlock_state();

    if (!connected) {
        return false;
    }

    now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (last_status == 0) {
        return (now - connect_time < kConnectTimeoutMs);
    }

    return (now - last_status < kStatusTimeoutMs);
}

extern "C" esp_err_t cnc_link_send_line(const char *line)
{
    int uart_num;
    char command[128];
    int written;

    if (!line || !line[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cnc_link_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    uart_num = s_link.config.uart_num;
    unlock_state();

    snprintf(command, sizeof(command), "%s\n", line);
    written = uart_write_bytes((uart_port_t)uart_num, command, strlen(command));
    return written > 0 ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t cnc_link_send_realtime(uint8_t byte)
{
    int uart_num;

    if (!cnc_link_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    uart_num = s_link.config.uart_num;
    unlock_state();

    return uart_write_bytes((uart_port_t)uart_num, &byte, 1) == 1 ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t cnc_link_request_status(void)
{
    uint32_t now_ms;
    uint32_t last_status_ms;

    if (!cnc_link_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    lock_state();
    last_status_ms = s_link.last_status_ms;
    unlock_state();

    if (last_status_ms != 0 && now_ms - last_status_ms < kDefaultStatusPeriodMs / 2) {
        return ESP_OK;
    }

    return cnc_link_send_realtime('?');
}

extern "C" esp_err_t cnc_link_jog(char axis, float delta_mm, float feed_mm_per_min)
{
    char command[64];
    char axis_name = (char)toupper((unsigned char)axis);

    if (!(axis_name == 'X' || axis_name == 'Y' || axis_name == 'Z' || axis_name == 'A')) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(command, sizeof(command), "$J=G91 G21 %c%.3f F%.1f", axis_name, delta_mm, feed_mm_per_min);
    return cnc_link_send_line(command);
}

extern "C" esp_err_t cnc_link_home(void)
{
    return cnc_link_send_line("$H");
}

extern "C" esp_err_t cnc_link_unlock(void)
{
    return cnc_link_send_line("$X");
}

extern "C" esp_err_t cnc_link_soft_reset(void)
{
    return cnc_link_send_realtime(0x18);
}

extern "C" void cnc_link_get_snapshot(cnc_link_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_link.initialized) {
        cnc_link_get_default_config(&snapshot->config);
        strlcpy(snapshot->machine_state, "Offline", sizeof(snapshot->machine_state));
        strlcpy(snapshot->last_message, "Idle", sizeof(snapshot->last_message));
        return;
    }

    lock_state();
    snapshot->connected = s_link.connected;
    snapshot->has_status = s_link.has_status;
    snapshot->machine_coordinates = s_link.machine_coordinates;
    snapshot->config = s_link.config;
    snapshot->x = s_link.x;
    snapshot->y = s_link.y;
    snapshot->z = s_link.z;
    snapshot->a = s_link.a;
    snapshot->feed = s_link.feed;
    snapshot->spindle = s_link.spindle;
    strlcpy(snapshot->machine_state, s_link.machine_state, sizeof(snapshot->machine_state));
    strlcpy(snapshot->last_message, s_link.last_message, sizeof(snapshot->last_message));
    unlock_state();
}

extern "C" void cnc_link_get_status(char *buffer, size_t buffer_len)
{
    cnc_link_snapshot_t snapshot;

    if (!buffer || buffer_len == 0) {
        return;
    }

    cnc_link_get_snapshot(&snapshot);
    if (!snapshot.connected) {
        snprintf(buffer, buffer_len, "Offline\nUART%d TX%d RX%d", snapshot.config.uart_num, snapshot.config.tx_pin, snapshot.config.rx_pin);
    } else if (!snapshot.has_status) {
        snprintf(buffer, buffer_len, "Connected\nWaiting for status\n%s", snapshot.last_message);
    } else {
        snprintf(buffer, buffer_len, "%s\n%s\nF%.0f S%.0f", snapshot.machine_state, snapshot.machine_coordinates ? "MPos" : "WPos", snapshot.feed, snapshot.spindle);
    }
}

extern "C" esp_err_t cnc_link_send_line_counted(const char *line)
{
    int len;

    if (!line || !line[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cnc_link_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    len = (int)strlen(line) + 1;

    if (!s_queue_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_queue_count >= kLineQueueSize) {
        xSemaphoreGive(s_queue_mutex);
        return ESP_ERR_NO_MEM;
    }

    if (s_chars_pending + len > kGrblRxMax) {
        xSemaphoreGive(s_queue_mutex);
        return ESP_FAIL;
    }

    s_line_queue[s_queue_head] = len;
    s_queue_head = (s_queue_head + 1) % kLineQueueSize;
    s_queue_count++;
    s_chars_pending += len;
    xSemaphoreGive(s_queue_mutex);

    return cnc_link_send_line(line);
}

extern "C" bool cnc_link_can_send(void)
{
    bool can = false;

    if (!cnc_link_is_connected()) {
        return false;
    }

    if (!s_queue_mutex) {
        return false;
    }

    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(50))) {
        can = (s_chars_pending + 8 < kGrblRxMax);
        xSemaphoreGive(s_queue_mutex);
    }

    return can;
}

extern "C" int cnc_link_get_pending_bytes(void)
{
    int val = 0;

    if (s_queue_mutex && xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(50))) {
        val = s_chars_pending;
        xSemaphoreGive(s_queue_mutex);
    }

    return val;
}

extern "C" uint32_t cnc_link_get_acked_count(void)
{
    uint32_t val = 0;

    if (s_queue_mutex && xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(50))) {
        val = s_acked_count;
        xSemaphoreGive(s_queue_mutex);
    }

    return val;
}
