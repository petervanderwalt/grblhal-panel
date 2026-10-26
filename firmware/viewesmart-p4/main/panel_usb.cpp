#include "panel_usb.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_dwc_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include <atomic>
#include <stdio.h>
#include <stdlib.h>

namespace panel {
static constexpr size_t RxSize = 16384;
static constexpr unsigned HostController = 1; // USB3: full-speed OTG, routed to GPIO24/25.
static SemaphoreHandle_t lock;
static StaticStreamBuffer_t rx_storage;
static StreamBufferHandle_t rx;
static cdc_acm_dev_hdl_t device;
static std::atomic<bool> host_installed{false}, driver_installed{false};
static std::atomic<bool> enabled{false}, connected{false}, lost{false};
static std::atomic<uint32_t> generation{0}, received{0}, transmitted{0};
static std::atomic<unsigned> vid{0}, pid{0};
static std::atomic<int> last_error{0};
static uint32_t next_open, open_failed_at;
static bool port_off;
static bool control_active;

static void host_task(void *) {
    for (;;) {
        uint32_t flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (err != ESP_OK) ESP_LOGE("panel_usb", "Host event: %s", esp_err_to_name(err));
        if (err == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) usb_host_device_free_all();
    }
}
static void new_device(usb_device_handle_t dev) {
    const usb_device_desc_t *desc = nullptr;
    if (usb_host_get_device_descriptor(dev, &desc) == ESP_OK) {
        vid = desc->idVendor; pid = desc->idProduct;
        ESP_LOGI("panel_usb", "Detected %04X:%04X", desc->idVendor, desc->idProduct);
    }
}
static bool receive(const uint8_t *data, size_t size, void *) {
    if (enabled && !lost) {
        if (xStreamBufferSend(rx, data, size, 0) != size) { last_error = ESP_ERR_NO_MEM; connected = false; lost = true; }
        else received += size;
    }
    return true;
}
static void event(const cdc_acm_host_dev_event_data_t *e, void *) {
    if (e->type == CDC_ACM_HOST_DEVICE_DISCONNECTED || e->type == CDC_ACM_HOST_ERROR) {
        connected = false; lost = true;
        last_error = e->type == CDC_ACM_HOST_ERROR ? e->data.error : ESP_ERR_NOT_FOUND;
    }
}
bool usb_prepare() {
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    if (!rx) {
        auto memory = (uint8_t *)heap_caps_malloc(RxSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return false;
        rx = xStreamBufferCreateStatic(RxSize + 1, 1, memory, &rx_storage);
        if (!rx) { free(memory); return false; }
    }
    if (!host_installed) {
        usb_host_config_t config = {};
        config.intr_flags = ESP_INTR_FLAG_LEVEL1;
        config.peripheral_map = 1U << HostController;
        esp_err_t err = usb_host_install(&config);
        if (err != ESP_OK) { last_error = err; return false; }
        // Route OTG to PHY0 (GPIO24/25) and unused native JTAG to PHY1; UART0 is unchanged.
        usb_serial_jtag_ll_phy_select(1);
        gpio_set_drive_capability(GPIO_NUM_24, GPIO_DRIVE_CAP_3);
        gpio_set_drive_capability(GPIO_NUM_25, GPIO_DRIVE_CAP_3);
        if (xTaskCreate(host_task, "panel_usb_host", 4096, nullptr, 10, nullptr) != pdPASS) {
            usb_host_uninstall(); last_error = ESP_ERR_NO_MEM; return false;
        }
        host_installed = true;
    }
    if (!driver_installed) {
        cdc_acm_host_driver_config_t config = {};
        config.driver_task_stack_size = 4096;
        config.driver_task_priority = 10;
        config.xCoreID = tskNO_AFFINITY;
        config.new_dev_cb = new_device;
        esp_err_t err = cdc_acm_host_install(&config);
        if (err != ESP_OK) { last_error = err; return false; }
        driver_installed = true;
    }
    return true;
}
bool usb_enable(bool on) {
    if (!on) { enabled = false; connected = false; return true; }
    if (!usb_prepare()) return false;
    enabled = true; return true;
}
// Only the link worker opens/closes devices; callbacks only queue bytes or flag loss.
void usb_poll() {
    if (!driver_installed || xSemaphoreTake(lock, 0) != pdTRUE) return;
    if (device && lost) {
        connected = false;
        cdc_acm_host_close(device); device = nullptr; control_active = false;
        xStreamBufferReset(rx); generation++;
    }
    // Logical disconnect keeps endpoint toggles intact; only physical loss closes CDC.
    if (device && !enabled && control_active) {
        cdc_acm_host_set_control_line_state(device, false, false);
        control_active = false;
        xStreamBufferReset(rx);
    }
    if (device && enabled && !connected && !lost) {
        esp_err_t err = cdc_acm_host_set_control_line_state(device, true, false);
        if (err == ESP_OK && enabled && !lost) {
            control_active = true; connected = true; last_error = 0; generation++;
        } else { last_error = err; lost = true; }
    }
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (port_off) {
        if (int32_t(now - next_open) >= 0) {
            last_error = usb_host_lib_set_root_port_power(true);
            port_off = false;
        }
        xSemaphoreGive(lock); return;
    }
    if (enabled && !device && int32_t(now - next_open) >= 0) {
        next_open = now + 1000; lost = false;
        cdc_acm_host_device_config_t config = {};
        config.connection_timeout_ms = 100;
        config.in_buffer_size = 256; config.out_buffer_size = 256;
        config.event_cb = event; config.data_cb = receive;
        esp_err_t err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, &config, &device);
        if (err == ESP_OK) {
            cdc_acm_line_coding_t coding = {};
            coding.dwDTERate = 115200; coding.bDataBits = 8;
            esp_err_t coding_err = cdc_acm_host_line_coding_set(device, &coding);
            ESP_LOGI("panel_usb", "Line coding: %s", esp_err_to_name(coding_err));
            err = cdc_acm_host_set_control_line_state(device, true, false);
            // Allow native CDC firmware to observe DTR before the first identification query.
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
            if (err == ESP_OK && enabled && !lost) {
                last_error = 0; open_failed_at = 0; control_active = true; connected = true; generation++;
                ESP_LOGI("panel_usb", "CDC ACM connected, interface 0");
            } else { last_error = err; connected = false; lost = true; }
        } else {
            last_error = err;
            if (!open_failed_at) open_failed_at = now;
            // A fast controller reboot can leave the root port waiting for enumeration.
            if (now - open_failed_at >= 5000 && usb_dwc_ll_hprt_get_conn_status(USB_DWC_LL_GET_HW(HostController))) {
                esp_err_t power_err = usb_host_lib_set_root_port_power(false);
                ESP_LOGW("panel_usb", "Recovering USB enumeration: %s", esp_err_to_name(power_err));
                if (power_err == ESP_OK) { port_off = true; next_open = now + 250; }
                open_failed_at = 0;
            }
        }
    }
    xSemaphoreGive(lock);
}
bool usb_connected() { return connected && enabled && !lost; }
uint32_t usb_generation() { return generation; }
bool usb_write(const uint8_t *bytes, size_t size) {
    if (!usb_connected() || xSemaphoreTake(lock, pdMS_TO_TICKS(150)) != pdTRUE) return false;
    esp_err_t err = usb_connected() && device ? cdc_acm_host_data_tx_blocking(device, bytes, size, 100) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) { last_error = err; connected = false; lost = true; }
    else transmitted += size;
    xSemaphoreGive(lock); return err == ESP_OK;
}
size_t usb_read(uint8_t *bytes, size_t size) {
    if (!usb_connected() || xSemaphoreTake(lock, 0) != pdTRUE) return 0;
    size_t n = xStreamBufferReceive(rx, bytes, size, 0);
    xSemaphoreGive(lock); return n;
}
void usb_flush() {
    // Drain through the single consumer, without resetting a live producer's ring.
    uint8_t discard[128];
    while (usb_read(discard, sizeof discard)) {}
}
void usb_status(char *text, size_t size) {
    if (usb_connected()) snprintf(text, size, "%04X:%04X / Connected", vid.load(), pid.load());
    else snprintf(text, size, "%s", enabled ? "Waiting for USB controller" : "Not connected");
}
} // namespace panel
extern "C" void panel_debug_usb(char *buffer, size_t size) {
    using namespace panel;
    usb_host_lib_info_t info = {};
    if (host_installed) usb_host_lib_info(&info);
    bool port_connected = host_installed && usb_dwc_ll_hprt_get_conn_status(USB_DWC_LL_GET_HW(HostController));
    snprintf(buffer, size, "{\"enabled\":%d,\"connected\":%d,\"vid\":%u,\"pid\":%u,\"generation\":%lu,\"rx_bytes\":%lu,\"tx_bytes\":%lu,\"error\":%d,\"port\":3,\"devices\":%d,\"clients\":%d,\"port_connected\":%d}",
             enabled.load(), usb_connected(), vid.load(), pid.load(), (unsigned long)generation.load(),
             (unsigned long)received.load(), (unsigned long)transmitted.load(), last_error.load(), info.num_devices, info.num_clients, port_connected);
}
