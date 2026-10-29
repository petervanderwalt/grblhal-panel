#include "panel_hardware.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
namespace panel {
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static uint32_t current = UINT32_MAX;
static bool ready;
void hardware_init() {
    // VIEWE schematic: D4 WS2812B DIN is GPIO30 via R60.
    rmt_tx_channel_config_t tx = {};
    tx.gpio_num = GPIO_NUM_30;
    tx.clk_src = RMT_CLK_SRC_DEFAULT;
    tx.resolution_hz = 10000000;
    tx.mem_block_symbols = 64;
    tx.trans_queue_depth = 1;
    rmt_copy_encoder_config_t copy = {};
    esp_err_t err = rmt_new_tx_channel(&tx, &channel);
    if (err == ESP_OK) err = rmt_new_copy_encoder(&copy, &encoder);
    if (err == ESP_OK) err = rmt_enable(channel);
    ready = err == ESP_OK;
    ESP_LOGI("panel_rgb", "GPIO30 WS2812B: %s", esp_err_to_name(err));
}
void hardware_status(const State &state) {
    uint32_t color = status_led_color(state);
    if (!ready || color == current) return;
    static rmt_symbol_word_t symbols[25];
    uint8_t bytes[] = {(uint8_t)(color >> 8), (uint8_t)(color >> 16), (uint8_t)color};
    unsigned index = 0;
    for (uint8_t byte : bytes) for (int bit = 7; bit >= 0; --bit) {
        bool one = byte & (1 << bit);
        symbols[index].level0 = 1; symbols[index].duration0 = one ? 9 : 3;
        symbols[index].level1 = 0; symbols[index].duration1 = one ? 3 : 9;
        ++index;
    }
    symbols[24].level0 = symbols[24].level1 = 0;
    symbols[24].duration0 = symbols[24].duration1 = 500;
    rmt_transmit_config_t config = {};
    esp_err_t err = rmt_transmit(channel, encoder, symbols, sizeof symbols, &config);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(channel, 20);
    if (err == ESP_OK) current = color;
    else { ready = false; ESP_LOGE("panel_rgb", "Update failed: %s", esp_err_to_name(err)); }
}
bool hardware_led_ready() { return ready; }
uint32_t hardware_led_color() { return current; }
}
