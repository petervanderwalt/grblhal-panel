"""Keep Hosted's PSRAM pool while giving IDF 5.3 SDIO internal DMA buffers."""
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "managed_components/espressif__esp_hosted/host/port/esp/freertos/src/port_esp_hosted_host_sdio.c"
text = source.read_text(encoding="utf8")
marker = "PANEL_SDIO_BOUNCE_V1"
if marker not in text:
    start = text.index("static esp_err_t sdio_read_fromio(")
    end = text.index("int hosted_sdio_deinit(", start)
    original = text[start:end]
    assert original.count("static esp_err_t ") == 2, "Hosted SDIO implementation changed"
    replacement = '''// PANEL_SDIO_BOUNCE_V1: IDF 5.3 block DMA rejects PSRAM pool buffers.
#include "esp_attr.h"
static DMA_ATTR __attribute__((aligned(64))) uint8_t panel_sdio_rx[ESP_BLOCK_SIZE];
static DMA_ATTR __attribute__((aligned(64))) uint8_t panel_sdio_tx[ESP_BLOCK_SIZE];

static esp_err_t sdio_read_fromio(sdmmc_card_t *card, uint32_t function,
                                uint32_t addr, uint8_t *data, uint16_t size)
{
    while (size) {
        size_t chunk = size >= ESP_BLOCK_SIZE ? ESP_BLOCK_SIZE : size;
        esp_err_t err = chunk == ESP_BLOCK_SIZE
            ? sdmmc_io_read_blocks(card, function, addr, panel_sdio_rx, chunk)
            : sdmmc_io_read_bytes(card, function, addr, panel_sdio_rx, chunk);
        if (err) return err;
        for (size_t i = 0; i < chunk; ++i)
            ((volatile uint8_t *)data)[i] = panel_sdio_rx[i];
        data += chunk;
        addr += chunk;
        size -= chunk;
    }
    return ESP_OK;
}

static esp_err_t sdio_write_toio(sdmmc_card_t *card, uint32_t function,
                               uint32_t addr, uint8_t *data, uint16_t size)
{
    while (size) {
        size_t chunk = size >= ESP_BLOCK_SIZE ? ESP_BLOCK_SIZE : size;
        for (size_t i = 0; i < chunk; ++i)
            panel_sdio_tx[i] = ((const volatile uint8_t *)data)[i];
        esp_err_t err = chunk == ESP_BLOCK_SIZE
            ? sdmmc_io_write_blocks(card, function, addr, panel_sdio_tx, chunk)
            : sdmmc_io_write_bytes(card, function, addr, panel_sdio_tx, chunk);
        if (err) return err;
        data += chunk;
        addr += chunk;
        size -= chunk;
    }
    return ESP_OK;
}

'''
    source.write_text(text[:start] + replacement + text[end:], encoding="utf8")
    print("Applied internal SDIO transfer staging")

# Preserve the PSRAM allocator on a fresh managed-component download too.
os_source = source.with_name("port_esp_hosted_host_os.c")
text = os_source.read_text(encoding="utf8")
start = text.index("void *hosted_malloc_align(")
end = text.index("void hosted_free_align(", start)
replacement = '''void *hosted_malloc_align(size_t size, size_t align)
{
    void *ptr = NULL;
#if CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM
    ptr = heap_caps_aligned_alloc(align, size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
#endif
    if (!ptr)
        ptr = heap_caps_aligned_alloc(align, size,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return ptr;
}

'''
if text[start:end] != replacement:
    os_source.write_text(text[:start] + replacement + text[end:], encoding="utf8")

# esp_lvgl_port 2.8 also supports newer LVGL; 9.2 has no swapped-format enum.
lvgl_port = Path(__file__).resolve().parents[1] / "managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c"
text = lvgl_port.read_text(encoding="utf8")
if "LV_COLOR_FORMAT_RGB565_SWAPPED" in text:
    assert text.count("LV_COLOR_FORMAT_RGB565_SWAPPED") == 3, "LVGL port format checks changed"
    text = text.replace("\n                        || disp_cfg->color_format == LV_COLOR_FORMAT_RGB565_SWAPPED", "")
    text = text.replace("\n                            || display_color_format == LV_COLOR_FORMAT_RGB565_SWAPPED", "")
    lvgl_port.write_text(text, encoding="utf8")
    print("Applied LVGL 9.2 display-format compatibility")
