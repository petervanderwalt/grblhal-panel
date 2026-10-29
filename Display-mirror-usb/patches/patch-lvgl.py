"""Keep esp_lvgl_port 2.8 compatible with the copied LVGL 9.2 configuration."""
from pathlib import Path
source = Path(__file__).resolve().parents[1] / "managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c"
text = source.read_text(encoding="utf8")
if "LV_COLOR_FORMAT_RGB565_SWAPPED" in text:
    assert text.count("LV_COLOR_FORMAT_RGB565_SWAPPED") == 3, "LVGL port format checks changed"
    text = text.replace("\n                        || disp_cfg->color_format == LV_COLOR_FORMAT_RGB565_SWAPPED", "")
    text = text.replace("\n                            || display_color_format == LV_COLOR_FORMAT_RGB565_SWAPPED", "")
    source.write_text(text, encoding="utf8")
