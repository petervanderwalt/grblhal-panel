#include "gcode_viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bsp/touch.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"

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

    size_t end = pos + 1;
    while (end < line.size() && (isdigit(static_cast<unsigned char>(line[end])) || line[end] == '.' || line[end] == '-')) {
        ++end;
    }

    return strtof(line.substr(pos + 1, end - pos - 1).c_str(), nullptr);
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

    const float cos_x = std::cos(s_viewer.cam_rot_x);
    const float sin_x = std::sin(s_viewer.cam_rot_x);
    const float y1 = y * cos_x - z * sin_x;
    const float z1 = y * sin_x + z * cos_x;

    const float cos_y = std::cos(s_viewer.cam_rot_y);
    const float sin_y = std::sin(s_viewer.cam_rot_y);
    const float x2 = x * cos_y - z1 * sin_y;

    out_x = static_cast<int>((s_viewer.width / 2.0f) + s_viewer.pan_x + (x2 * s_viewer.model_scale));
    out_y = static_cast<int>((s_viewer.height / 2.0f) + s_viewer.pan_y - (y1 * s_viewer.model_scale));
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
}

extern "C" void gcode_viewer_detach(void)
{
    s_viewer.canvas = nullptr;
}

extern "C" bool gcode_viewer_load_file(const char *path, char *status, size_t status_len)
{
    FILE *file = fopen(path, "r");
    char line_buf[256];
    long file_size = 0;
    long bytes_read = 0;
    int last_progress = -1;

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
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        if (line.empty() || line[0] == ';') {
            continue;
        }

        if (line.find("G90") != std::string::npos) state.abs = true;
        if (line.find("G91") != std::string::npos) state.abs = false;
        if (line.find("G21") != std::string::npos) state.mm = true;
        if (line.find("G20") != std::string::npos) state.mm = false;
        if (line.find("G17") != std::string::npos) state.plane = 0;
        if (line.find("G18") != std::string::npos) state.plane = 1;
        if (line.find("G19") != std::string::npos) state.plane = 2;
        if (line.find("G0") != std::string::npos) state.mode = 0;
        if (line.find("G1") != std::string::npos) state.mode = 1;
        if (line.find("G2") != std::string::npos) state.mode = 2;
        if (line.find("G3") != std::string::npos) state.mode = 3;

        if (line.find('X') != std::string::npos || line.find('Y') != std::string::npos || line.find('Z') != std::string::npos ||
            (state.mode > 1 && (line.find('I') != std::string::npos || line.find('J') != std::string::npos || line.find('K') != std::string::npos))) {
            const float tx = parse_axis_value(line, 'X', state.abs ? state.x : 0.0f) + (state.abs ? 0.0f : state.x);
            const float ty = parse_axis_value(line, 'Y', state.abs ? state.y : 0.0f) + (state.abs ? 0.0f : state.y);
            const float tz = parse_axis_value(line, 'Z', state.abs ? state.z : 0.0f) + (state.abs ? 0.0f : state.z);
            const float scale = state.mm ? 1.0f : 25.4f;

            if (state.mode <= 1) {
                add_point(tx * scale, ty * scale, tz * scale, static_cast<uint8_t>(state.mode));
            } else {
                MachineState scaled = state;
                scaled.x *= scale;
                scaled.y *= scale;
                scaled.z *= scale;
                parse_arc(tx * scale, ty * scale, tz * scale,
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

    if (s_viewer.vertices.size() < 2) {
        snprintf(status, status_len, "No toolpath in %s", path);
        return false;
    }

    reset_view();
    render_model();
    report_progress(100);
    snprintf(status, status_len, "Loaded %u segments from %s", static_cast<unsigned>(s_viewer.vertices.size()), strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
    return true;
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
        s_viewer.cam_rot_x = -1.57f;
        s_viewer.cam_rot_y = 0.0f;
        break;
    case GCODE_VIEW_PRESET_BOTTOM:
        s_viewer.cam_rot_x = 1.57f;
        s_viewer.cam_rot_y = 0.0f;
        break;
    case GCODE_VIEW_PRESET_FRONT:
        s_viewer.cam_rot_x = 0.0f;
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
    s_viewer.model_scale *= factor;
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
