#include "ui_remote.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "panel_debug.h"
extern void panel_debug_storage(char *,size_t);
extern void panel_debug_usb(char *,size_t);
extern bool panel_ui_preview_server_field(const char *);
extern bool panel_storage_capture_pause(bool);
#include "panel_ui.h"
#include "wifi_bridge.h"

static lv_indev_t *remote_input;
static lv_point_t touch_point;
static bool pressed;
static SemaphoreHandle_t tap_done;
static bool tap_pending;
static bool release_phase;
static int hold_ms;
static lv_point_t swipe_start, swipe_end;
static int swipe_step;

static void input_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = touch_point;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void reply(const char *text)
{
    /* Share stdout's lock with SDK logs so each protocol line stays intact. */
    fputs(text, stdout);
    fflush(stdout);
}

// The console owns this packet buffer; reserve it before UI heap fragmentation.
static DRAM_ATTR char screenshot_line[BSP_LCD_H_RES * 8 + 48];

static void screenshot_frame(void)
{
    const size_t size = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    uint16_t *pixels = heap_caps_aligned_alloc(128, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *line = screenshot_line;
    if (!pixels) {
        reply("\n@UGERR memory\n");
        free(pixels);
        return;
    }
    if (!bsp_display_lock(2000)) {
        reply("\n@UGERR display_busy\n");
        free(pixels);
        return;
    }
    /* Copy the last displayed frame; rendering stays on the LVGL task. */
    printf("@UGDBG screenshot stack %lu\n", (unsigned long)uxTaskGetStackHighWaterMark(NULL));

    reply("@UGDBG screenshot capture ready\n");
    esp_err_t err = bsp_display_copy_framebuffer(pixels, size);
    reply("@UGDBG screenshot pixels staged\n");
    // Keep interrupt-masked cache maintenance bounded.
    for (size_t offset=0; err==ESP_OK && offset<size; offset+=16384) {
        size_t chunk=size-offset<16384?size-offset:16384;
        err=esp_cache_msync((uint8_t *)pixels+offset,chunk,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        vTaskDelay(1);
    }
    reply("@UGDBG screenshot frame copied\n");
    bsp_display_unlock();
    if (err != ESP_OK) {
        reply("\n@UGERR framebuffer\n");
    } else {
        /* Keep PSRAM checksum work bounded so both cores can service their ticks. */
        uint32_t checksum = 0;
        for (size_t offset = 0; offset < size; offset += 16384) {
            size_t chunk = size - offset < 16384 ? size - offset : 16384;
            checksum = esp_rom_crc32_le(checksum, (uint8_t *)pixels + offset, chunk);
            vTaskDelay(1);
        }
        snprintf(line, 48, "\n@UGSHOT %d %d %08lx\n", BSP_LCD_H_RES, BSP_LCD_V_RES,
                 (unsigned long)checksum);
        reply(line);
        for (int y = 0; y < BSP_LCD_V_RES; y++) {
            int n = sprintf(line, "@UGROW %d ", y);
            for (int x = 0; x < BSP_LCD_H_RES;) {
                uint16_t color = pixels[y * BSP_LCD_H_RES + x];
                int run = 1;
                while (x + run < BSP_LCD_H_RES &&
                       pixels[y * BSP_LCD_H_RES + x + run] == color) run++;
                n += sprintf(line + n, "%04x%04x", run, color);
                x += run;
            }
            line[n++] = '\n';
            line[n] = 0;
            reply(line);
            vTaskDelay(1);
        }
        reply("@UGEND\n");
    }
    free(pixels);
}

static void screenshot(void)
{
    if (panel_storage_capture_pause(true)) screenshot_frame();
    else reply("\n@UGERR storage_busy\n");
    panel_storage_capture_pause(false);
}

/* UI callbacks run on the LVGL task, including screen creation and deletion. */
static void tap_tick(lv_timer_t *timer)
{
    if (!release_phase) {
        lv_indev_read(remote_input);
        release_phase = true;
        lv_timer_set_period(timer, swipe_step >= 0 ? 40 : hold_ms);
    } else if (swipe_step >= 0 && swipe_step < 10) {
        swipe_step++;
        touch_point.x = swipe_start.x + (swipe_end.x - swipe_start.x) * swipe_step / 10;
        touch_point.y = swipe_start.y + (swipe_end.y - swipe_start.y) * swipe_step / 10;
        lv_indev_read(remote_input);
    } else {
        pressed = false;
        lv_indev_read(remote_input);
        lv_timer_delete(timer);
        tap_pending = false;
        xSemaphoreGive(tap_done);
    }
}

static void command(const char *line)
{
    // One console task owns these buffers; leave stack space for framebuffer row staging.
    static char out[1040], payload[1024];
    int x, y, x2 = 0, y2 = 0, consumed = 0, duration = 100;
    bool swipe = sscanf(line, "UG SWIPE %d %d %d %d%n", &x, &y, &x2, &y2, &consumed) == 4 && line[consumed] == 0;
    if (!strcmp(line,"UG TOUCH ON") || !strcmp(line,"UG TOUCH OFF")) {
        bsp_display_lock(0);
        lv_indev_t *physical = bsp_display_get_input_dev();
        if (physical) lv_indev_enable(physical,!strcmp(line,"UG TOUCH ON"));
        bsp_display_unlock();
        reply("@UGOK TOUCH\n");
    } else if (!strcmp(line, "UG TEST")) {
        reply(panel_debug_test() ? "@UGOK TEST\n" : "@UGERR tests_failed\n");
    } else if (!strcmp(line, "UG DEMO ON") || !strcmp(line, "UG DEMO OFF")) {
        panel_debug_demo(!strcmp(line, "UG DEMO ON"));
        reply("@UGOK DEMO\n");
    } else if (!strncmp(line, "UG RX ", 6)) {
        reply(panel_debug_receive(line + 6) ? "@UGOK RX\n" : "@UGERR preview_required\n");
    } else if (!strcmp(line, "UG STATE") || !strcmp(line, "UG LAST") || !strcmp(line,"UG PREFS")) {
        if (!strcmp(line, "UG STATE")) panel_debug_state(payload,sizeof payload);
        else if (!strcmp(line,"UG PREFS")) { bsp_display_lock(0);panel_ui_settings(payload,sizeof payload);bsp_display_unlock(); }
        else panel_debug_last(payload,sizeof payload);
        snprintf(out,sizeof out,"@UGDATA %s\n",payload);reply(out);
    } else if (!strncmp(line,"UG PREVIEW SERVER ",18)) {
        bsp_display_lock(0);bool ok=panel_ui_preview_server_field(line+18);bsp_display_unlock();reply(ok?"@UGOK PREVIEW\n":"@UGERR server_field\n");
    } else if (!strcmp(line,"UG USB STATE")) {
        panel_debug_usb(payload,sizeof payload);snprintf(out,sizeof out,"@UGDATA %s\n",payload);reply(out);
    } else if (!strcmp(line,"UG SD STATE")) {
        panel_debug_storage(payload,sizeof payload);snprintf(out,sizeof out,"@UGDATA %s\n",payload);reply(out);
    } else if (!strcmp(line,"UG WIFI STATE")) {
        wifi_bridge_debug(payload,sizeof payload);
        snprintf(out,sizeof out,"@UGDATA %s\n",payload);reply(out);
    } else if (!strncmp(line,"UG WIFI PASSWORD ",17)) {
        const char *hex = line + 17; size_t n = strlen(hex); char password[65] = {0};
        bool valid = n <= 128 && n % 2 == 0;
        for (size_t i = 0; valid && i < n; i += 2) {
            unsigned byte; int used = 0;
            if (sscanf(hex+i,"%2x%n",&byte,&used) != 1 || used != 2 || byte < 32 || byte > 126) valid = false;
            else password[i/2] = byte;
        }
        bsp_display_lock(0);
        bool ok = valid && panel_ui_wifi_password(password);
        bsp_display_unlock(); memset(password,0,sizeof password);
        reply(ok ? "@UGOK WIFI\n" : "@UGERR wifi_password_field\n");
    } else if (!strncmp(line, "UG SETTING FIELD ", 17)) {
        const char *hex = line + 17; size_t n = strlen(hex); char text[513] = {0};
        bool valid = n <= 1024 && n % 2 == 0;
        for (size_t i = 0; valid && i < n; i += 2) {
            unsigned byte; int used = 0;
            if (sscanf(hex + i, "%2x%n", &byte, &used) != 1 || used != 2 || byte < 32 || byte > 126) valid = false;
            else text[i / 2] = byte;
        }
        bsp_display_lock(0);
        bool ok = valid && panel_ui_setting_field(text);
        bsp_display_unlock(); memset(text, 0, sizeof text);
        reply(ok ? "@UGOK SETTING\n" : "@UGERR setting_field\n");
    } else if (!strncmp(line, "UG MACRO FIELD ", 15)) {
        unsigned index; int used = 0;
        bool valid = sscanf(line + 15, "%u %n", &index, &used) == 1 && used > 0;
        const char *hex = line + 15 + used; size_t n = strlen(hex); static char text[769];
        memset(text, 0, sizeof text);
        valid = valid && n <= 1536 && n % 2 == 0;
        for (size_t i = 0; valid && i < n; i += 2) {
            unsigned byte; int consumed = 0;
            if (sscanf(hex + i, "%2x%n", &byte, &consumed) != 1 || consumed != 2 ||
                (byte != 10 && byte != 13 && (byte < 32 || byte > 126))) valid = false;
            else text[i / 2] = byte;
        }
        bsp_display_lock(0);
        bool ok = valid && panel_ui_macro_field(index, text);
        bsp_display_unlock();
        reply(ok ? "@UGOK MACRO\n" : "@UGERR macro_field\n");
    } else if (!strncmp(line, "UG ACTUATOR FIELD ", 18)) {
        unsigned index; int used = 0;
        bool valid = sscanf(line + 18, "%u %n", &index, &used) == 1 && used > 0;
        const char *hex = line + 18 + used; size_t n = strlen(hex); char text[49] = {0};
        valid = valid && n <= 96 && n % 2 == 0;
        for (size_t i = 0; valid && i < n; i += 2) {
            unsigned byte; int consumed = 0;
            if (sscanf(hex + i, "%2x%n", &byte, &consumed) != 1 || consumed != 2 || byte < 32 || byte > 126)
                valid = false;
            else text[i / 2] = byte;
        }
        bsp_display_lock(0);
        bool ok = valid && panel_ui_actuator_field(index, text);
        bsp_display_unlock();
        reply(ok ? "@UGOK ACTUATOR\n" : "@UGERR actuator_field\n");
    } else if (!strcmp(line, "UG SHOT")) {
        screenshot();
    } else if ((swipe || (sscanf(line, "UG TAP %d %d%n", &x, &y, &consumed) == 2 && line[consumed] == 0) ||
                (sscanf(line, "UG HOLD %d %d %d%n", &x, &y, &duration, &consumed) == 3 && line[consumed] == 0)) &&
               line[consumed] == 0 && x >= 0 && x < BSP_LCD_H_RES &&
               y >= 0 && y < BSP_LCD_V_RES && duration >= 50 && duration <= 2000 &&
               (!swipe || (x2 >= 0 && x2 < BSP_LCD_H_RES && y2 >= 0 && y2 < BSP_LCD_V_RES))) {
        if (!bsp_display_lock(2000)) {
            reply("@UGERR display_busy\n");
            return;
        }
        if (tap_pending) {
            bsp_display_unlock();
            reply("@UGERR touch_busy\n");
            return;
        }
        xSemaphoreTake(tap_done, 0);
        touch_point = (lv_point_t){x, y};
        swipe_start = touch_point;
        swipe_end = (lv_point_t){x2, y2};
        swipe_step = swipe ? 0 : -1;
        pressed = true;
        release_phase = false;
        hold_ms = duration;
        tap_pending = true;
        lv_timer_t *timer = lv_timer_create(tap_tick, 100, NULL);
        if (!timer) {
            tap_pending = false;
            pressed = false;
            bsp_display_unlock();
            reply("@UGERR memory\n");
            return;
        }
        bsp_display_unlock();
        if (xSemaphoreTake(tap_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
            reply("@UGERR touch_timeout\n");
            return;
        }
        reply("@UGOK TAP\n");
    } else {
        reply("@UGERR command\n");
    }
}

static void console_task(void *arg)
{
    (void)arg;
    static char line[2048];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if (ch == '\r') continue;
        if (ch == '\n') {
            line[used] = 0;
            if (!overflow && used) command(line);
            else if (overflow) reply("@UGERR line_too_long\n");
            used = 0;
            overflow = false;
        } else if (used < sizeof(line) - 1 && !overflow) {
            line[used++] = ch;
        } else {
            overflow = true;
        }
    }
}

void ui_remote_start(void)
{
    tap_done = xSemaphoreCreateBinary();
    assert(tap_done);
    /* Buffer a full hex-encoded command while UI/NVS work briefly holds the console. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 0, 0, NULL, 0));
    bsp_display_lock(0);
    remote_input = lv_indev_create();
    assert(remote_input);
    lv_indev_set_type(remote_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(remote_input, lv_display_get_default());
    lv_indev_set_read_cb(remote_input, input_read);
    lv_indev_set_mode(remote_input, LV_INDEV_MODE_EVENT);
    bsp_display_unlock();
    BaseType_t created = xTaskCreate(console_task, "ui_remote", 12 * 1024, NULL, 2, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
