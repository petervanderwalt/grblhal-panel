#include "gcode_viewer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bsp/touch.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr size_t kMaxPoints = 400000;
constexpr float kMinSegmentLength = 0.05f;
constexpr float kArcResolutionMm = 2.0f;
constexpr float kCoordScale = 100.0f;
constexpr uint16_t kColorBg = 0x0841;
constexpr uint16_t kColorRapid = 0x8000;
constexpr uint16_t kColorLinear = 0xFFFF;
constexpr uint16_t kColorArc = 0x07FF;

struct PackedVertex {
    int32_t x;
    int32_t y;
    int32_t z_and_type;
};

struct MachineState {
    float x;
    float y;
    float z;
    bool abs;
    bool mm;
    int plane;
    int mode;
};

struct ViewerState {
    lv_obj_t *canvas = nullptr;
    uint16_t *buffer = nullptr;
    int width = 0;
    int height = 0;
    std::vector<PackedVertex> vertices;
    float cam_rot_y = 0.78f;
    float cam_rot_x = -0.78f;
    float cos_x = 1, sin_x = 0, cos_y = 1, sin_y = 0;
    float model_scale = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    lv_point_t last_press_point = {0, 0};
    float last_pinch_distance = 0.0f;
    bool dragging = false;
    bool pinch_active = false;
    int render_count = -1;
};

ViewerState s_viewer;
gcode_viewer_progress_cb_t s_progress_cb = nullptr;
void *s_progress_user_data = nullptr;

static int32_t encode_coord(float value)
{
    return static_cast<int32_t>(std::lround(value * kCoordScale));
}

static float decode_coord(int32_t value)
{
    return static_cast<float>(value) / kCoordScale;
}

static int32_t pack_z_and_type(float z, uint8_t type)
{
    const uint32_t z_bits = static_cast<uint32_t>(encode_coord(z)) & 0x3FFFFFFF;
    return static_cast<int32_t>(z_bits | (static_cast<uint32_t>(type & 0x3) << 30));
}

static float unpack_z(int32_t packed)
{
    int32_t raw = packed & 0x3FFFFFFF;
    if (raw & 0x20000000) {
        raw |= static_cast<int32_t>(0xC0000000);
    }
    return decode_coord(raw);
}

static uint8_t unpack_type(int32_t packed)
{
    return static_cast<uint8_t>((static_cast<uint32_t>(packed) >> 30) & 0x3);
}

static float vertex_x(const PackedVertex &vertex)
{
    return decode_coord(vertex.x);
}

static float vertex_y(const PackedVertex &vertex)
{
    return decode_coord(vertex.y);
}

static float vertex_z(const PackedVertex &vertex)
{
    return unpack_z(vertex.z_and_type);
}

static uint8_t vertex_type(const PackedVertex &vertex)
{
    return unpack_type(vertex.z_and_type);
}

static float parse_axis_value(const std::string &line, char axis, float fallback)
{
    const size_t pos = line.find(axis);
    if (pos == std::string::npos) {
        return fallback;
    }

    char *end;
    float value = strtof(line.c_str() + pos + 1, &end);
    return end == line.c_str() + pos + 1 || !std::isfinite(value) ? fallback : value;
}
static float axis_target(const std::string &line, char axis, float current, const MachineState &state)
{
    const float scale = state.mm ? 1.f : 25.4f;
    return parse_axis_value(line, axis, state.abs ? current / scale : 0.f) * scale + (state.abs ? 0.f : current);
}
static bool modal_block(std::string &line, MachineState &state)
{
    int comment = 0;
    std::string clean;
    for (unsigned char c : line) {
        if (c == '(') { comment++; continue; }
        if (c == ')' && comment) { comment--; continue; }
        if (!comment && c == ';') break;
        if (!comment) clean += static_cast<char>(std::toupper(c));
    }
    line.swap(clean);
    if (line.find_first_of("#[]") != std::string::npos) return false;
    for (const char *p = line.c_str(); *p;) {
        char letter = *p++;
        if (letter != 'G' && letter != 'M') continue;
        char *end;
        float value = strtof(p, &end);
        if (p == end) continue;
        p = end;
        if (letter == 'M') { if (value == 98 || value == 99) return false; continue; }
        if (!std::isfinite(value) || value < 0 || value > 1000 || value != std::floor(value)) return false;
        switch ((int)value) {
        case 0: case 1: case 2: case 3: state.mode = (int)value; break;
        case 17: state.plane = 0; break;
        case 18: state.plane = 1; break;
        case 19: state.plane = 2; break;
        case 20: state.mm = false; break;
        case 21: state.mm = true; break;
        case 90: state.abs = true; break;
        case 91: state.abs = false; break;
        case 4: case 40: case 49: case 54: case 55: case 56: case 57: case 58: case 59:
        case 61: case 64: case 80: case 93: case 94: case 95: break;
        default: return false;
        }
    }
    return !(state.mode > 1 && line.find('R') != std::string::npos);
}

static void report_progress(int progress)
{
    if (s_progress_cb) {
        s_progress_cb(progress, s_progress_user_data);
    }
}

static void clear_buffer()
{
    if (!s_viewer.buffer) {
        return;
    }

    std::fill_n(s_viewer.buffer, static_cast<size_t>(s_viewer.width * s_viewer.height), kColorBg);
}

static void put_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= s_viewer.width || y >= s_viewer.height) {
        return;
    }

    s_viewer.buffer[y * s_viewer.width + x] = color;
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    // Clip before Bresenham so zooming cannot walk millions of off-screen pixels.
    float vx = (float)x1 - x0, vy = (float)y1 - y0, start = 0, end = 1;
    float p[] = {-vx, vx, -vy, vy};
    float q[] = {(float)x0, (float)s_viewer.width - 1 - x0, (float)y0, (float)s_viewer.height - 1 - y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) { if (q[i] < 0) return; continue; }
        float t = q[i] / p[i];
        if (p[i] < 0) start = std::max(start, t); else end = std::min(end, t);
        if (start > end) return;
    }
    x1 = (int)(x0 + end * vx); y1 = (int)(y0 + end * vy);
    x0 = (int)(x0 + start * vx); y0 = (int)(y0 + start * vy);
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void project_point(float in_x, float in_y, float in_z, int &out_x, int &out_y)
{
    const float x = in_x - s_viewer.center_x;
    const float y = in_y - s_viewer.center_y;
    const float z = in_z - s_viewer.center_z;

    const float cos_x = s_viewer.cos_x;
    const float sin_x = s_viewer.sin_x;
    const float y1 = y * cos_x - z * sin_x;
    const float z1 = y * sin_x + z * cos_x;

    const float cos_y = s_viewer.cos_y;
    const float sin_y = s_viewer.sin_y;
    const float x2 = x * cos_y - z1 * sin_y;

    out_x = static_cast<int>(std::clamp((s_viewer.width / 2.0f) + s_viewer.pan_x + (x2 * s_viewer.model_scale), -10000000.f, 10000000.f));
    out_y = static_cast<int>(std::clamp((s_viewer.height / 2.0f) + s_viewer.pan_y - (y1 * s_viewer.model_scale), -10000000.f, 10000000.f));
}

static void update_bounds(float x, float y, float z)
{
    s_viewer.min_x = std::min(s_viewer.min_x, x);
    s_viewer.max_x = std::max(s_viewer.max_x, x);
    s_viewer.min_y = std::min(s_viewer.min_y, y);
    s_viewer.max_y = std::max(s_viewer.max_y, y);
    s_viewer.min_z = std::min(s_viewer.min_z, z);
    s_viewer.max_z = std::max(s_viewer.max_z, z);
}

static void add_point(float x, float y, float z, uint8_t type)
{
    if (s_viewer.vertices.size() >= kMaxPoints) {
        return;
    }

    if (!s_viewer.vertices.empty()) {
        const PackedVertex &last = s_viewer.vertices.back();
        const float dx = x - vertex_x(last);
        const float dy = y - vertex_y(last);
        const float dz = z - vertex_z(last);
        if ((dx * dx + dy * dy + dz * dz) < (kMinSegmentLength * kMinSegmentLength)) {
            return;
        }
    }

    s_viewer.vertices.push_back({encode_coord(x), encode_coord(y), pack_z_and_type(z, type)});
    update_bounds(x, y, z);
}

static void parse_arc(float tx, float ty, float tz, float i, float j, float k, const MachineState &st)
{
    const float sx = st.x;
    const float sy = st.y;
    const float sz = st.z;
    float a1s;
    float a2s;
    float a1e;
    float a2e;
    float off1;
    float off2;
    float lin_s;
    float lin_e;

    if (st.plane == 1) {
        a1s = sz; a2s = sx; a1e = tz; a2e = tx; off1 = k; off2 = i; lin_s = sy; lin_e = ty;
    } else if (st.plane == 2) {
        a1s = sy; a2s = sz; a1e = ty; a2e = tz; off1 = j; off2 = k; lin_s = sx; lin_e = tx;
    } else {
        a1s = sx; a2s = sy; a1e = tx; a2e = ty; off1 = i; off2 = j; lin_s = sz; lin_e = tz;
    }

    const float center_x = a1s + off1;
    const float center_y = a2s + off2;
    const float radius = std::sqrt(off1 * off1 + off2 * off2);
    const float angle_start = std::atan2(a2s - center_y, a1s - center_x);
    const float angle_end = std::atan2(a2e - center_y, a1e - center_x);
    float diff = angle_end - angle_start;

    if (st.mode == 2 && diff >= 0.0f) {
        diff -= 2.0f * static_cast<float>(M_PI);
    }
    if (st.mode == 3 && diff <= 0.0f) {
        diff += 2.0f * static_cast<float>(M_PI);
    }

    int segments = static_cast<int>(std::abs(diff) * radius / kArcResolutionMm);
    if (segments < 1) {
        segments = 1;
    }
    segments = std::min(segments, (int)kMaxPoints);

    const float angle_step = diff / segments;
    const float linear_step = (lin_e - lin_s) / segments;

    for (int step = 1; step <= segments; ++step) {
        const float angle = angle_start + angle_step * step;
        const float linear = lin_s + linear_step * step;
        const float c1 = center_x + std::cos(angle) * radius;
        const float c2 = center_y + std::sin(angle) * radius;

        if (st.plane == 1) {
            add_point(c2, linear, c1, 2);
        } else if (st.plane == 2) {
            add_point(linear, c1, c2, 2);
        } else {
            add_point(c1, c2, linear, 2);
        }
    }
}

static void render_model()
{
    s_viewer.cos_x = std::cos(s_viewer.cam_rot_x);
    s_viewer.sin_x = std::sin(s_viewer.cam_rot_x);
    s_viewer.cos_y = std::cos(s_viewer.cam_rot_y);
    s_viewer.sin_y = std::sin(s_viewer.cam_rot_y);
    clear_buffer();

    if (s_viewer.vertices.size() < 2) {
        if (s_viewer.canvas) {
            lv_obj_invalidate(s_viewer.canvas);
        }
        return;
    }

    size_t limit = (s_viewer.render_count >= 0) ? std::min((size_t)s_viewer.render_count, s_viewer.vertices.size()) : s_viewer.vertices.size();
    for (size_t i = 1; i < limit; ++i) {
        const PackedVertex &v1 = s_viewer.vertices[i - 1];
        const PackedVertex &v2 = s_viewer.vertices[i];
        int x0;
        int y0;
        int x1;
        int y1;
        uint16_t color = kColorLinear;

        project_point(vertex_x(v1), vertex_y(v1), vertex_z(v1), x0, y0);
        project_point(vertex_x(v2), vertex_y(v2), vertex_z(v2), x1, y1);

        if ((x0 < -10 && x1 < -10) || (x0 > s_viewer.width + 10 && x1 > s_viewer.width + 10)) {
            continue;
        }
        if ((y0 < -10 && y1 < -10) || (y0 > s_viewer.height + 10 && y1 > s_viewer.height + 10)) {
            continue;
        }

        if (vertex_type(v2) == 0) {
            color = kColorRapid;
        } else if (vertex_type(v2) == 2) {
            color = kColorArc;
        }

        draw_line(x0, y0, x1, y1, color);
    }

    if (s_viewer.canvas) {
        lv_obj_invalidate(s_viewer.canvas);
    }
}

static void reset_view()
{
    s_viewer.center_x = (s_viewer.min_x + s_viewer.max_x) * 0.5f;
    s_viewer.center_y = (s_viewer.min_y + s_viewer.max_y) * 0.5f;
    s_viewer.center_z = (s_viewer.min_z + s_viewer.max_z) * 0.5f;
    const float max_dim = std::max({s_viewer.max_x - s_viewer.min_x, s_viewer.max_y - s_viewer.min_y, s_viewer.max_z - s_viewer.min_z});
    s_viewer.model_scale = max_dim > 0.0f ? ((std::min(s_viewer.width, s_viewer.height) * 0.7f) / max_dim) : 1.0f;
    s_viewer.pan_x = 0.0f;
    s_viewer.pan_y = 0.0f;
}

static void viewer_input_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);

    if (indev == nullptr) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &s_viewer.last_press_point);
        s_viewer.dragging = false;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        esp_lcd_touch_handle_t touch = bsp_touch_get_handle();
        if (touch != nullptr) {
            esp_lcd_touch_point_data_t touch_points[2] = {};
            uint8_t touch_count = 0;
            if (esp_lcd_touch_get_data(touch, touch_points, &touch_count, 2) == ESP_OK && touch_count >= 2) {
                const float dx = static_cast<float>((int)touch_points[0].x - (int)touch_points[1].x);
                const float dy = static_cast<float>((int)touch_points[0].y - (int)touch_points[1].y);
                const float pinch_distance = std::sqrt((dx * dx) + (dy * dy));

                if (s_viewer.pinch_active && s_viewer.last_pinch_distance > 1.0f && pinch_distance > 1.0f) {
                    const float zoom_factor = pinch_distance / s_viewer.last_pinch_distance;
                    if (zoom_factor > 0.0f && std::fabs(zoom_factor - 1.0f) > 0.015f) {
                        gcode_viewer_zoom(zoom_factor);
                    }
                }

                s_viewer.last_pinch_distance = pinch_distance;
                s_viewer.pinch_active = true;
                s_viewer.dragging = false;
                return;
            }
        }

        if (s_viewer.pinch_active) {
            lv_indev_get_point(indev, &s_viewer.last_press_point);
            s_viewer.pinch_active = false;
            s_viewer.last_pinch_distance = 0.0f;
        }

        lv_point_t point;
        lv_indev_get_point(indev, &point);

        const int dx = point.x - s_viewer.last_press_point.x;
        const int dy = point.y - s_viewer.last_press_point.y;
        if (dx != 0 || dy != 0) {
            s_viewer.dragging = true;
            s_viewer.pan_x += static_cast<float>(dx);
            s_viewer.pan_y += static_cast<float>(dy);
            s_viewer.last_press_point = point;
            render_model();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        s_viewer.dragging = false;
        s_viewer.pinch_active = false;
        s_viewer.last_pinch_distance = 0.0f;
        return;
    }

    if (code == LV_EVENT_GESTURE) {
        switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_LEFT:
            gcode_viewer_rotate(0.0f, -0.20f);
            break;
        case LV_DIR_RIGHT:
            gcode_viewer_rotate(0.0f, 0.20f);
            break;
        case LV_DIR_TOP:
            gcode_viewer_zoom(1.15f);
            break;
        case LV_DIR_BOTTOM:
            gcode_viewer_zoom(0.87f);
            break;
        default:
            break;
        }
    }
}

} // namespace

extern "C" void gcode_viewer_attach(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    if (s_viewer.buffer == nullptr) {
        s_viewer.buffer = static_cast<uint16_t *>(
            heap_caps_malloc(1024 * 600 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!s_viewer.buffer) return;

    s_viewer.width = width;
    s_viewer.height = height;
    s_viewer.canvas = lv_canvas_create(parent);
    lv_obj_set_size(s_viewer.canvas, width, height);
    lv_obj_set_style_radius(s_viewer.canvas, 12, 0);
    lv_obj_set_style_clip_corner(s_viewer.canvas, true, 0);
    lv_obj_set_style_border_width(s_viewer.canvas, 0, 0);
    lv_obj_set_style_bg_color(s_viewer.canvas, lv_color_hex(0xf8f9fa), 0);
    lv_obj_clear_flag(s_viewer.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_viewer.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_viewer.canvas, viewer_input_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_viewer.canvas, viewer_input_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_viewer.canvas, viewer_input_event_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_viewer.canvas, viewer_input_event_cb, LV_EVENT_GESTURE, nullptr);
    lv_canvas_set_buffer(s_viewer.canvas, s_viewer.buffer, width, height, LV_COLOR_FORMAT_RGB565);
    clear_buffer();
    reset_view();
    render_model();
}

extern "C" void gcode_viewer_detach(void)
{
    s_viewer.canvas = nullptr;
}

static bool parse_stream(FILE *file, const char *path, char *status, size_t status_len)
{
    char line_buf[768];
    long file_size = 0;
    long bytes_read = 0;
    int last_progress = -1;
    int blocks = 0;

    if (!file) {
        snprintf(status, status_len, "Open failed: %s", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) == 0) {
        file_size = ftell(file);
        rewind(file);
    }

    s_viewer.vertices.clear();
    s_viewer.min_x = s_viewer.min_y = s_viewer.min_z = 1e9f;
    s_viewer.max_x = s_viewer.max_y = s_viewer.max_z = -1e9f;
    s_viewer.vertices.reserve(60000);

    MachineState state = {0.0f, 0.0f, 0.0f, true, true, 0, 0};
    s_viewer.vertices.push_back({encode_coord(0.0f), encode_coord(0.0f), pack_z_and_type(0.0f, 0)});
    report_progress(0);

    while (fgets(line_buf, sizeof(line_buf), file) != nullptr) {
        bytes_read += (long)strlen(line_buf);
        std::string line(line_buf);
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
        if (!modal_block(line, state)) {
            fclose(file);
            s_viewer.vertices.clear();
            snprintf(status, status_len, "Unsupported preview code (macros, offsets, cycles or R arcs). SD run is still available.");
            return false;
        }
        if (line.empty()) continue;
        if (++blocks % 256 == 0) vTaskDelay(1);

        if (line.find('X') != std::string::npos || line.find('Y') != std::string::npos || line.find('Z') != std::string::npos ||
            (state.mode > 1 && (line.find('I') != std::string::npos || line.find('J') != std::string::npos || line.find('K') != std::string::npos))) {
            const float scale = state.mm ? 1.0f : 25.4f;
            const float tx = axis_target(line, 'X', state.x, state);
            const float ty = axis_target(line, 'Y', state.y, state);
            const float tz = axis_target(line, 'Z', state.z, state);
            if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
                std::fabs(tx) > 1000000 || std::fabs(ty) > 1000000 || std::fabs(tz) > 1000000) {
                fclose(file); s_viewer.vertices.clear();
                snprintf(status, status_len, "Preview coordinates out of range"); return false;
            }

            if (state.mode <= 1) {
                add_point(tx, ty, tz, static_cast<uint8_t>(state.mode));
            } else {
                MachineState scaled = state;
                for (char axis : {'I', 'J', 'K'}) {
                    if (std::fabs(parse_axis_value(line, axis, 0) * scale) > 1000000) {
                        fclose(file); s_viewer.vertices.clear();
                        snprintf(status, status_len, "Preview arc out of range"); return false;
                    }
                }
                parse_arc(tx, ty, tz,
                          parse_axis_value(line, 'I', 0.0f) * scale,
                          parse_axis_value(line, 'J', 0.0f) * scale,
                          parse_axis_value(line, 'K', 0.0f) * scale,
                          scaled);
            }

            state.x = tx;
            state.y = ty;
            state.z = tz;
        }

        if (file_size > 0) {
            int progress = (int)((bytes_read * 100L) / file_size);
            if (progress > 100) {
                progress = 100;
            }
            if (progress != last_progress) {
                last_progress = progress;
                report_progress(progress);
            }
        }
    }

    fclose(file);

    if (s_viewer.vertices.size() >= kMaxPoints) {
        s_viewer.vertices.clear();
        snprintf(status, status_len, "Preview segment limit exceeded; no partial toolpath shown");
        return false;
    }

    if (s_viewer.vertices.size() < 2) {
        snprintf(status, status_len, "No toolpath in %s", path);
        return false;
    }

    report_progress(100);
    snprintf(status, status_len, "Loaded %u segments from %s", static_cast<unsigned>(s_viewer.vertices.size()), strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
    return true;
}
extern "C" bool gcode_viewer_load_file(const char *path, char *status, size_t status_len)
{
    bool ok = parse_stream(fopen(path, "r"), path, status, status_len);
    if (ok) { reset_view(); render_model(); }
    return ok;
}
extern "C" bool gcode_viewer_parse_memory(const char *data, size_t size, char *status, size_t status_len)
{
    return data && size && parse_stream(fmemopen((void *)data, size, "r"), "controller SD", status, status_len);
}
extern "C" bool gcode_viewer_parser_test(void)
{
    MachineState state = {0,0,0,true,true,0,0};
    std::string a = "G21 G90 G17 (G2 X99) ;G3";
    if (!modal_block(a,state) || state.mode != 0 || a.find('X') != std::string::npos) return false;
    a = "G1 X+12.5";
    if (!modal_block(a,state) || state.mode != 1 || parse_axis_value(a,'X',0) != 12.5f) return false;
    a = "G20 G91 G18";
    if (!modal_block(a,state) || state.mode != 1 || state.mm || state.abs || state.plane != 1) return false;
    a = "X1";
    if (std::fabs(axis_target(a,'X',25.4f,state) - 50.8f) > .001f) return false;
    a = "G21 G90";
    if (!modal_block(a,state) || std::fabs(axis_target(a,'X',25.4f,state) - 25.4f) > .001f) return false;
    a = "G81 X10";
    if (modal_block(a,state)) return false;
    a = "G1 X#1";
    return !modal_block(a,state);
}

extern "C" void gcode_viewer_set_progress_callback(gcode_viewer_progress_cb_t cb, void *user_data)
{
    s_progress_cb = cb;
    s_progress_user_data = user_data;
}

extern "C" void gcode_viewer_set_preset(gcode_view_preset_t preset)
{
    switch (preset) {
    case GCODE_VIEW_PRESET_TOP:
        s_viewer.cam_rot_x = 0.0f;
        s_viewer.cam_rot_y = 0.0f;
        break;
    case GCODE_VIEW_PRESET_BOTTOM:
        s_viewer.cam_rot_x = 3.14159265f;
        s_viewer.cam_rot_y = 0.0f;
        break;
    case GCODE_VIEW_PRESET_FRONT:
        s_viewer.cam_rot_x = -1.57079633f;
        s_viewer.cam_rot_y = 0.0f;
        break;
    case GCODE_VIEW_PRESET_BACK:
        s_viewer.cam_rot_x = 0.0f;
        s_viewer.cam_rot_y = 3.14f;
        break;
    case GCODE_VIEW_PRESET_LEFT:
        s_viewer.cam_rot_x = 0.0f;
        s_viewer.cam_rot_y = -1.57f;
        break;
    case GCODE_VIEW_PRESET_RIGHT:
        s_viewer.cam_rot_x = 0.0f;
        s_viewer.cam_rot_y = 1.57f;
        break;
    case GCODE_VIEW_PRESET_ISO:
    default:
        s_viewer.cam_rot_x = -0.78f;
        s_viewer.cam_rot_y = 0.78f;
        break;
    }

    render_model();
}

extern "C" void gcode_viewer_rotate(float delta_x, float delta_y)
{
    s_viewer.cam_rot_x += delta_x;
    s_viewer.cam_rot_y += delta_y;
    render_model();
}

extern "C" void gcode_viewer_pan(int delta_x, int delta_y)
{
    s_viewer.pan_x += static_cast<float>(delta_x);
    s_viewer.pan_y += static_cast<float>(delta_y);
    render_model();
}

extern "C" void gcode_viewer_zoom(float factor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    s_viewer.model_scale = std::clamp(s_viewer.model_scale * factor, .0001f, 10000.f);
    render_model();
}

extern "C" void gcode_viewer_refresh(void)
{
    render_model();
}

extern "C" bool gcode_viewer_has_model(void)
{
    return s_viewer.vertices.size() > 1;
}

extern "C" void gcode_viewer_set_render_count(int count)
{
    s_viewer.render_count = count;
    if (count >= 0) {
        render_model();
    }
}

extern "C" int gcode_viewer_get_vertex_count(void)
{
    return static_cast<int>(s_viewer.vertices.size());
}
