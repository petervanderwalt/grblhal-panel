/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "driver/jpeg_decode.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_cache.h"
#include "bsp/display.h"
#include "app_lcd.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

static const char *TAG = "app_lcd";
// Survives watchdog resets: 1=header, 2=decode, 3=copy start, 4=copy wait, 6=done.
static RTC_NOINIT_ATTR volatile uint32_t trace[3];
static esp_lcd_panel_handle_t panel;
static jpeg_decoder_handle_t decoder;
static uint8_t *decoded;
static void *framebuffer;
static size_t decoded_size;
static SemaphoreHandle_t copy_done;
_Static_assert(EXAMPLE_LCD_H_RES % 16 == 0, "DMA copy requires JPEG-aligned row width");

static bool IRAM_ATTR copied(esp_lcd_panel_handle_t handle,
                            esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    return wake == pdTRUE;
}
static const jpeg_decode_cfg_t decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};

static void check_jpeg_rejection(void)
{
    // Espressif's regression case: table ID 4 used to write outside qt_tbl.
    const uint8_t bad_table[71] = {0xff, 0xd8, 0xff, 0xdb, 0, 0x43, 4, [7 ... 70] = 1};
    const uint8_t truncated[] = {0xff, 0xd8, 0xff, 0xe0, 0xff, 0xff};
    jpeg_decode_picture_info_t info;
    uint32_t size;
    assert(jpeg_decoder_get_info(truncated, sizeof(truncated), &info) != ESP_OK);
    assert(jpeg_decoder_process(decoder, &decode_cfg, bad_table, sizeof(bad_table),
                                decoded, decoded_size, &size) != ESP_OK);
    assert(heap_caps_check_integrity_all(true));
    ESP_LOGI(TAG, "PASS: malformed JPEG rejected; heap intact");
}

void app_lcd_draw(uint8_t *buf, uint32_t len, uint16_t width, uint16_t height)
{
    static unsigned frames;
    bool first = frames < 3;
    if (first) ESP_LOGI(TAG, "First frame: %u bytes, %ux%u", (unsigned)len, width, height);
    trace[1] = 1;
    jpeg_decode_picture_info_t info;
    if (width != EXAMPLE_LCD_H_RES || height != EXAMPLE_LCD_V_RES ||
        jpeg_decoder_get_info(buf, len, &info) != ESP_OK ||
        info.width != width || info.height != height) return;
    if (first) ESP_LOGI(TAG, "JPEG header accepted; decode start");
    trace[1] = 2;
    uint32_t out_size = 0;
    if (jpeg_decoder_process(decoder, &decode_cfg, buf, len, decoded,
                             decoded_size, &out_size) != ESP_OK) return;
    if (first) ESP_LOGI(TAG, "Decode complete: %u bytes; copy start", (unsigned)out_size);
    trace[1] = 3;
    // JPEG may pad 600 rows to 608; only copy visible rows to the LCD.
    const size_t stride = ((width + 15U) & ~15U) * 2U;
    if (out_size < stride * height) return;
    // Copy only visible rows. Serialize LCD DMA with JPEG's shared DMA2D engine.
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, width, height, decoded));
    trace[1] = 4;
    ESP_ERROR_CHECK(xSemaphoreTake(copy_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
    trace[1] = 6;
    trace[2] = frames + 1;
    if (first) ESP_LOGI(TAG, "First desktop frame displayed");
    if (++frames % 30 == 0) ESP_LOGI(TAG, "Displayed frames: %u", frames);
}

esp_err_t app_lcd_init(void)
{
    if (trace[0] == 0x4D4F4E31) ESP_LOGW(TAG, "Previous run: reset=%d phase=%lu frames=%lu", esp_reset_reason(), (unsigned long)trace[1], (unsigned long)trace[2]);
    trace[0] = 0x4D4F4E31; trace[1] = 0; trace[2] = 0;
    const jpeg_decode_engine_cfg_t engine = { .intr_priority = 1, .timeout_ms = 100 };
    ESP_RETURN_ON_ERROR(jpeg_new_decoder_engine(&engine, &decoder), TAG, "JPEG init");
    const jpeg_decode_memory_alloc_cfg_t memory = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    decoded = jpeg_alloc_decoder_mem(((EXAMPLE_LCD_H_RES + 15U) & ~15U) *
                                    ((EXAMPLE_LCD_V_RES + 15U) & ~15U) * 2U,
                                    &memory, &decoded_size);
    ESP_RETURN_ON_FALSE(decoded, ESP_ERR_NO_MEM, TAG, "JPEG buffer");
    check_jpeg_rejection();
    copy_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(copy_done, ESP_ERR_NO_MEM, TAG, "LCD copy semaphore");
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(bsp_display_new(NULL, &panel, &io), TAG, "LCD init");
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = { .on_color_trans_done = copied };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, copy_done), TAG, "LCD callback");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &framebuffer), TAG, "LCD buffer");
    memset(framebuffer, 0, EXAMPLE_LCD_BUF_LEN);
    ESP_RETURN_ON_ERROR(esp_cache_msync(framebuffer, EXAMPLE_LCD_BUF_LEN, ESP_CACHE_MSYNC_FLAG_DIR_C2M), TAG, "LCD clear");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "Backlight");
    ESP_LOGI(TAG, "USB monitor ready: 1024x600 RGB565, USB2 high-speed USB");
    return ESP_OK;
}
