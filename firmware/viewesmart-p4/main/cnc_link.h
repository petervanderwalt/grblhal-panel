#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    bool use_rs485_power;
} cnc_link_config_t;

typedef struct {
    bool connected;
    bool has_status;
    bool machine_coordinates;
    cnc_link_config_t config;
    float x;
    float y;
    float z;
    float a;
    float feed;
    float spindle;
    char machine_state[16];
    char last_message[64];
} cnc_link_snapshot_t;

#define CNC_LINK_RX_BUFFER_SIZE 128

esp_err_t cnc_link_init(void);
void cnc_link_get_default_config(cnc_link_config_t *config);
esp_err_t cnc_link_connect(const cnc_link_config_t *config);
void cnc_link_disconnect(void);
bool cnc_link_is_connected(void);
esp_err_t cnc_link_send_line(const char *line);
esp_err_t cnc_link_send_realtime(uint8_t byte);
esp_err_t cnc_link_request_status(void);
esp_err_t cnc_link_jog(char axis, float delta_mm, float feed_mm_per_min);
esp_err_t cnc_link_home(void);
esp_err_t cnc_link_unlock(void);
esp_err_t cnc_link_soft_reset(void);
void cnc_link_get_snapshot(cnc_link_snapshot_t *snapshot);
void cnc_link_get_status(char *buffer, size_t buffer_len);
esp_err_t cnc_link_send_line_counted(const char *line);
bool cnc_link_can_send(void);
int cnc_link_get_pending_bytes(void);
uint32_t cnc_link_get_acked_count(void);

#ifdef __cplusplus
}
#endif
