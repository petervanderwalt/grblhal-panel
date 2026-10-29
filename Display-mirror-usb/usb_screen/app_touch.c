/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#include "app_usb.h"
#include "usb_descriptors.h"

static const char *TAG = "app_touch";

static esp_lcd_touch_handle_t tp = NULL;

static void app_touch_task(void *arg)
{
    uint8_t touchpad_cnt = 0;
    bool send_press = false;
    while (1) {
        esp_err_t error = esp_lcd_touch_read_data(tp);
        if (error != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        esp_lcd_touch_point_data_t touch_points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
        if (esp_lcd_touch_get_data(tp, touch_points, &touchpad_cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        hid_report_t report = {0};
        if (touchpad_cnt > 0) {
            if (!send_press) ESP_LOGI(TAG, "Touch down: %u,%u (%u contacts)", touch_points[0].x, touch_points[0].y, touchpad_cnt);
            report.report_id = REPORT_ID_TOUCH;
            int i = 0;
            for (i = 0; i < touchpad_cnt; i++) {
                report.touch_report.data[i].index = touch_points[i].track_id;
                report.touch_report.data[i].press_down = 1;
                report.touch_report.data[i].x = touch_points[i].x;
                report.touch_report.data[i].y = touch_points[i].y;
                report.touch_report.data[i].width = touch_points[i].strength;
                report.touch_report.data[i].height = touch_points[i].strength;
                /*!< >= LOG_LEVEL_DEBUG */
#if CONFIG_LOG_DEFAULT_LEVEL >= 4
                /*!< For debug */
                printf("(%d: %d, %d. %d) ", touch_points[i].track_id, touch_points[i].x, touch_points[i].y, touch_points[i].strength);
#endif
            }
#if CONFIG_LOG_DEFAULT_LEVEL >= 4
            /*!< For debug */
            printf("\n");
#endif
            ESP_LOGD(TAG, "touchpad cnt: %d\n", touchpad_cnt);
            report.touch_report.cnt = touchpad_cnt;
#if CFG_TUD_HID
            tinyusb_hid_keyboard_report(report);
#endif
            send_press = true;
        } else if (send_press) {
            send_press = false;
            report.report_id = REPORT_ID_TOUCH;
#if CFG_TUD_HID
            tinyusb_hid_keyboard_report(report);
#endif
            ESP_LOGD(TAG, "send release %d", touchpad_cnt);
        }
        // Reading from the GT911 at a time shorter than this may result in false reports.
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

esp_err_t app_touch_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &tp), TAG, "Touch init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_set_mirror_x(tp, false), TAG, "Touch X orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_set_mirror_y(tp, false), TAG, "Touch Y orientation");
    if (xTaskCreatePinnedToCore(app_touch_task, "app_touch_task", 4096, NULL, CONFIG_TOUCH_TASK_PRIORITY, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "USB touch ready (5 contacts)");
    return ESP_OK;
}
