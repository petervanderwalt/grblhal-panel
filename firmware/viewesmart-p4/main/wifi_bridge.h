#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WIFI_BRIDGE_MAX_SCAN_RESULTS 12
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_bridge_scan_result_t;
typedef struct {
    bool ready, scanning, connecting, connected;
    int8_t rssi;
    unsigned count, revision;
    char status[96], ssid[33], ip[16];
    wifi_bridge_scan_result_t networks[WIFI_BRIDGE_MAX_SCAN_RESULTS];
} wifi_bridge_state_t;
esp_err_t wifi_bridge_init(void);
esp_err_t wifi_bridge_scan(void);
esp_err_t wifi_bridge_connect(const char *ssid, const char *password);
esp_err_t wifi_bridge_disconnect(void);
void wifi_bridge_snapshot(wifi_bridge_state_t *state);
void wifi_bridge_debug(char *buffer, size_t size);
#ifdef __cplusplus
}
#endif
