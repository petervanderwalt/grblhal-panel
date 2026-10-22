#include "wifi_bridge.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "panel_handoff.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCAN, CONNECT, DISCONNECT, SCANNED, LOST, GOT_IP };
typedef struct {
    int type, reason;
    char ssid[33], password[65], ip[16];
} request_t;
static QueueHandle_t queue;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_bridge_state_t state = {.status = "Not connected"};
static wifi_config_t config;
static bool initialized, ignore_disconnect, retry_pending;
static uint32_t started, retry_since;
#define LOCK() portENTER_CRITICAL(&lock)
#define UNLOCK() portEXIT_CRITICAL(&lock)
static uint32_t now(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static void status(const char *text) {
    LOCK();
    snprintf(state.status, sizeof state.status, "%s", text);
    UNLOCK();
}
static void failure(esp_err_t err) {
    retry_pending = false;
    char message[96];
    snprintf(message, sizeof message, "Wi-Fi: %s", esp_err_to_name(err));
    LOCK();
    state.scanning = state.connecting = false;
    UNLOCK();
    status(message);
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    request_t r = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        r.type = SCANNED;
        r.reason = ((wifi_event_sta_scan_done_t *)data)->status;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        r.type = LOST;
        r.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        r.type = GOT_IP;
        ip_event_got_ip_t *ip = data;
        snprintf(r.ip, sizeof r.ip, IPSTR, IP2STR(&ip->ip_info.ip));
    } else
        return;
    xQueueSend(queue, &r, 0);
}
static esp_err_t start(void) {
    if (initialized)
        return ESP_OK;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK)
        return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") && !esp_netif_create_default_wifi_sta())
        return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK)
        return err;
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL)) != ESP_OK)
        return err;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL)) != ESP_OK)
        return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
        return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK)
        return err;
    if ((err = esp_wifi_start()) != ESP_OK)
        return err;
    initialized = true;
    LOCK();
    state.ready = true;
    UNLOCK();
    return ESP_OK;
}
static void save(void) {
    nvs_handle_t h;
    if (nvs_open("panelwifi", NVS_READWRITE, &h) != ESP_OK) {
        status("Connected; could not save network");
        return;
    }
    char ssid[33] = {0}, password[65] = {0};
    memcpy(ssid, config.sta.ssid, 32);
    memcpy(password, config.sta.password, 64);
    esp_err_t err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK)
        err = nvs_set_str(h, "password", password);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    memset(password, 0, sizeof password);
    if (err != ESP_OK)
        status("Connected; could not save network");
}
static void connect_network(const request_t *r) {
    retry_pending = false;
    wifi_bridge_state_t previous;
    wifi_bridge_snapshot(&previous);
    esp_err_t err = start();
    if (err != ESP_OK) {
        failure(err);
        return;
    }
    if (previous.connected) {
        ignore_disconnect = true;
        err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ignore_disconnect = false;
            failure(err);
            return;
        }
    }
    memset(&config, 0, sizeof config);
    memcpy(config.sta.ssid, r->ssid, strlen(r->ssid));
    memcpy(config.sta.password, r->password, strlen(r->password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    LOCK();
    state.connected = false;
    state.connecting = true;
    state.ip[0] = 0;
    snprintf(state.ssid, sizeof state.ssid, "%s", r->ssid);
    UNLOCK();
    started = now();
    status("Connecting...");
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK)
        err = esp_wifi_connect();
    if (err != ESP_OK)
        failure(err);
}
static void worker(void *arg) {
    (void)arg;
    uint32_t signal_at = 0;
    request_t r = {.type = CONNECT};
    nvs_handle_t h;
    if (nvs_open("panelwifi", NVS_READONLY, &h) == ESP_OK) {
        size_t sn = sizeof r.ssid, pn = sizeof r.password;
        bool saved = nvs_get_str(h, "ssid", r.ssid, &sn) == ESP_OK &&
                     nvs_get_str(h, "password", r.password, &pn) == ESP_OK;
        nvs_close(h);
        if (saved && r.ssid[0])
            connect_network(&r);
        memset(&r, 0, sizeof r);
    }
    for (;;) {
        if (xQueueReceive(queue, &r, pdMS_TO_TICKS(100))) {
            if (r.type == SCAN) {
                esp_err_t err = start();
                if (err == ESP_OK) {
                    wifi_scan_config_t scan = {.show_hidden = false};
                    started = now();
                    err = esp_wifi_scan_start(&scan, false);
                }
                if (err != ESP_OK)
                    failure(err);
            } else if (r.type == CONNECT)
                connect_network(&r);
            else if (r.type == DISCONNECT) {
                retry_pending = false;
                wifi_bridge_state_t previous;
                wifi_bridge_snapshot(&previous);
                ignore_disconnect = previous.connected;
                esp_err_t err = initialized && previous.connected ? esp_wifi_disconnect() : ESP_OK;
                LOCK();
                state.connected = state.connecting = false;
                state.ip[0] = 0;
                UNLOCK();
                if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
                    failure(err);
                else
                    status("Disconnected");
            } else if (r.type == SCANNED) {
                uint16_t count = WIFI_BRIDGE_MAX_SCAN_RESULTS;
                wifi_ap_record_t *records = calloc(count, sizeof *records);
                esp_err_t err = records ? esp_wifi_scan_get_ap_records(&count, records) : ESP_ERR_NO_MEM;
                LOCK();
                state.count = 0;
                if (err == ESP_OK && !r.reason)
                    for (int i = 0; i < count; i++) {
                        if (!records[i].ssid[0])
                            continue;
                        bool duplicate = false;
                        for (unsigned j = 0; j < state.count; j++)
                            if (!strcmp(state.networks[j].ssid, (char *)records[i].ssid))
                                duplicate = true;
                        if (duplicate)
                            continue;
                        wifi_bridge_scan_result_t *network = &state.networks[state.count++];
                        snprintf(network->ssid, sizeof network->ssid, "%.32s", records[i].ssid);
                        network->rssi = records[i].rssi;
                        network->authmode = records[i].authmode;
                    }
                state.scanning = false;
                state.revision++;
                UNLOCK();
                free(records);
                if (err != ESP_OK || r.reason)
                    failure(err != ESP_OK ? err : ESP_FAIL);
                else
                    status("Select a network");
            } else if (r.type == LOST) {
                if (ignore_disconnect)
                    ignore_disconnect = false;
                else {
                    LOCK();
                    bool reconnect = state.connected || state.connecting;
                    if (state.connected)
                        started = now();
                    state.connected = false;
                    state.connecting = reconnect;
                    state.ip[0] = 0;
                    UNLOCK();
                    // The C6 can miss the first authentication after startup.
                    retry_pending = reconnect;
                    retry_since = now();
                    char text[96];
                    snprintf(text, sizeof text, reconnect ? "Retrying connection (%d)..." :
                             "Disconnected (%d)", r.reason);
                    status(text);
                }
            } else if (r.type == GOT_IP) {
                retry_pending = false;
                LOCK();
                state.connected = true;
                state.connecting = false;
                snprintf(state.ip, sizeof state.ip, "%s", r.ip);
                UNLOCK();
                status("Connected");
                save();
                panel_handoff_start();
            }
            memset(r.password, 0, sizeof r.password);
        }
        wifi_bridge_state_t current;
        wifi_bridge_snapshot(&current);
        if (retry_pending && !current.scanning && now() - retry_since >= 1000 &&
            now() - started <= 30000) {
            retry_pending = false;
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
                failure(err);
        }
        if (current.connected && !current.scanning && now() - signal_at >= 5000) {
            signal_at = now();
            int rssi = 0;
            if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK && rssi < 0 && rssi >= -127) {
                LOCK();
                state.rssi = rssi;
                UNLOCK();
            }
        }
        if ((current.scanning || current.connecting) && now() - started > 30000) {
            if (current.scanning)
                esp_wifi_scan_stop();
            else {
                ignore_disconnect = true;
                esp_wifi_disconnect();
            }
            failure(ESP_ERR_TIMEOUT);
        }
    }
}
esp_err_t wifi_bridge_init(void) {
    if (queue)
        return ESP_OK;
    queue = xQueueCreate(10, sizeof(request_t));
    if (!queue)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker, "panel_wifi", 8192, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(queue);
        queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
static esp_err_t request(request_t *r) {
    esp_err_t err = wifi_bridge_init();
    if (err != ESP_OK)
        return err;
    LOCK();
    bool busy = state.scanning || state.connecting;
    if (!busy) {
        started = now();
        state.scanning = r->type == SCAN;
        state.connecting = r->type == CONNECT;
    }
    UNLOCK();
    if (busy)
        return ESP_ERR_INVALID_STATE;
    if (r->type == SCAN)
        status("Scanning...");
    if (r->type == CONNECT)
        status("Connecting...");
    if (xQueueSend(queue, r, 0) != pdTRUE) {
        failure(ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t wifi_bridge_scan(void) {
    request_t r = {.type = SCAN};
    return request(&r);
}
esp_err_t wifi_bridge_disconnect(void) {
    request_t r = {.type = DISCONNECT};
    return request(&r);
}
esp_err_t wifi_bridge_connect(const char *ssid, const char *password) {
    size_t sn = ssid ? strlen(ssid) : 0, pn = password ? strlen(password) : 0;
    if (!sn || sn > 32 || (pn && pn < 8) || pn > 64)
        return ESP_ERR_INVALID_ARG;
    if (pn == 64)
        for (size_t i = 0; i < pn; i++)
            if (!isxdigit((unsigned char)password[i]))
                return ESP_ERR_INVALID_ARG;
    request_t r = {.type = CONNECT};
    memcpy(r.ssid, ssid, sn);
    if (pn)
        memcpy(r.password, password, pn);
    esp_err_t err = request(&r);
    memset(r.password, 0, sizeof r.password);
    return err;
}
void wifi_bridge_snapshot(wifi_bridge_state_t *out) {
    LOCK();
    *out = state;
    UNLOCK();
}
void wifi_bridge_debug(char *buffer, size_t size) {
    wifi_bridge_state_t s;
    wifi_bridge_snapshot(&s);
    cJSON *j = cJSON_CreateObject();
    if (!j) {
        snprintf(buffer, size, "{}");
        return;
    }
    cJSON_AddBoolToObject(j, "scanning", s.scanning);
    cJSON_AddBoolToObject(j, "connecting", s.connecting);
    cJSON_AddBoolToObject(j, "connected", s.connected);
    cJSON_AddNumberToObject(j, "rssi", s.rssi);
    cJSON_AddStringToObject(j, "ssid", s.ssid);
    cJSON_AddStringToObject(j, "ip", s.ip);
    cJSON_AddStringToObject(j, "status", s.status);
    cJSON *list = cJSON_AddArrayToObject(j, "networks");
    for (unsigned i = 0; i < s.count; i++)
        cJSON_AddItemToArray(list, cJSON_CreateString(s.networks[i].ssid));
    if (!cJSON_PrintPreallocated(j, buffer, size, false))
        snprintf(buffer, size, "{}");
    cJSON_Delete(j);
}
