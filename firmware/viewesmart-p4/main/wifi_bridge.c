#include "wifi_bridge.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_bridge";

static bool s_initialized;
static bool s_connected;
static bool s_connecting;
static volatile bool s_scanning;
static wifi_bridge_scan_result_t s_scan_results[WIFI_BRIDGE_MAX_SCAN_RESULTS];
static size_t s_scan_result_count;
static char s_status[96] = "Wi-Fi idle";

static void wifi_bridge_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            s_connected = false;
            s_connecting = false;
            snprintf(s_status, sizeof(s_status), "Disconnected (reason %u)", (unsigned)event->reason);
        } else if (event_id == WIFI_EVENT_SCAN_DONE) {
            uint16_t count = WIFI_BRIDGE_MAX_SCAN_RESULTS;

            s_scan_result_count = 0;
            s_scanning = false;
            wifi_ap_record_t *ap_records = calloc(WIFI_BRIDGE_MAX_SCAN_RESULTS, sizeof(wifi_ap_record_t));
            if (ap_records && esp_wifi_scan_get_ap_records(&count, ap_records) == ESP_OK) {
                s_scan_result_count = count;
                for (size_t i = 0; i < count; ++i) {
                    strlcpy(s_scan_results[i].ssid, (const char *)ap_records[i].ssid, sizeof(s_scan_results[i].ssid));
                    s_scan_results[i].rssi = ap_records[i].rssi;
                    s_scan_results[i].authmode = ap_records[i].authmode;
                }
                snprintf(s_status, sizeof(s_status), "Scan complete (%u found)", (unsigned)count);
            } else {
                strlcpy(s_status, "Scan complete (read failed)", sizeof(s_status));
            }
            free(ap_records);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_connected = true;
        s_connecting = false;
        snprintf(s_status, sizeof(s_status), "Connected " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_bridge_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_bridge_event_handler, NULL), TAG, "wifi handler register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_bridge_event_handler, NULL), TAG, "ip handler register failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    s_initialized = true;
    s_scan_result_count = 0;
    s_connected = false;
    s_connecting = false;
    strlcpy(s_status, "Wi-Fi ready", sizeof(s_status));
    return ESP_OK;
}

esp_err_t wifi_bridge_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "ssid required");
    ESP_RETURN_ON_ERROR(wifi_bridge_init(), TAG, "wifi init failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "esp_wifi_disconnect failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect failed");

    s_connected = false;
    s_connecting = true;
    snprintf(s_status, sizeof(s_status), "Connecting to %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_bridge_disconnect(void)
{
    if (!s_initialized) {
        strlcpy(s_status, "Wi-Fi idle", sizeof(s_status));
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }

    s_connected = false;
    s_connecting = false;
    strlcpy(s_status, "Disconnected", sizeof(s_status));
    return ESP_OK;
}

esp_err_t wifi_bridge_scan(void)
{
    ESP_RETURN_ON_ERROR(wifi_bridge_init(), TAG, "wifi init failed");

    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };

    s_scan_result_count = 0;
    s_scanning = true;
    strlcpy(s_status, "Scanning...", sizeof(s_status));
    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        s_scanning = false;
        return err;
    }

    return ESP_OK;
}

bool wifi_bridge_is_scanning(void)
{
    return s_scanning;
}

size_t wifi_bridge_get_scan_results(wifi_bridge_scan_result_t *results, size_t max_results)
{
    if (!results || max_results == 0 || s_scan_result_count == 0) {
        return 0;
    }

    size_t count = s_scan_result_count < max_results ? s_scan_result_count : max_results;
    memcpy(results, s_scan_results, count * sizeof(wifi_bridge_scan_result_t));
    return count;
}

bool wifi_bridge_is_connected(void)
{
    return s_connected;
}

void wifi_bridge_get_status(char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    snprintf(buffer, buffer_len, "%s", s_status);
}
