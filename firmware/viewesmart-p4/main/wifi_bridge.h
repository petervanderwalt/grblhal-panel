#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_BRIDGE_MAX_SCAN_RESULTS 12

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_bridge_scan_result_t;

esp_err_t wifi_bridge_init(void);
esp_err_t wifi_bridge_connect(const char *ssid, const char *password);
esp_err_t wifi_bridge_disconnect(void);
esp_err_t wifi_bridge_scan(void);
bool wifi_bridge_is_scanning(void);
size_t wifi_bridge_get_scan_results(wifi_bridge_scan_result_t *results, size_t max_results);
bool wifi_bridge_is_connected(void);
void wifi_bridge_get_status(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
