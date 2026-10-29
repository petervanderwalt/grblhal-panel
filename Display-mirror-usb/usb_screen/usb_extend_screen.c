/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#if CONFIG_HID_TOUCH_ENABLE
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#endif
#include "app_usb.h"
#include "usb_descriptors.h"
#include "esp_log.h"
#if CONFIG_HID_TOUCH_ENABLE
#include "app_touch.h"
#endif
#include "app_lcd.h"

static const char *TAG = "usb_extend_screen";

extern bool panel_memory_test(void);

void app_main(void)
{
    ESP_LOGI(TAG, "USB extend screen example");
    ESP_ERROR_CHECK(panel_memory_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(app_lcd_init());
    ESP_ERROR_CHECK(app_usb_init());
#if CONFIG_HID_TOUCH_ENABLE
    ESP_ERROR_CHECK(app_touch_init());
#endif
}
