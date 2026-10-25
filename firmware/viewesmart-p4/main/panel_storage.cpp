#include "panel_storage.h"
#include "panel_file_head.h"
#include "bsp/esp32_p4_ultra.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "wifi_bridge.h"
#include <algorithm>
#include <assert.h>
#include <atomic>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
namespace panel {
static constexpr const char *ROOT = "/sdcard", *CACHE = "/sdcard/.panel-previews";
static const char *views[] = {"top", "bottom", "front", "back", "left", "right", "isometric", "perspective"};
static constexpr unsigned MaxStorageFiles = 512;
struct Entry {
    File file;
    char hash[65];
    bool ready, tried;
};
struct Catalog {
    Entry entries[MaxStorageFiles];
    unsigned order[MaxStorageFiles] = {};
    unsigned count = 0, revision = 0;
    int percent = -1;
    bool mounted = false, scanning = false;
    char message[160] = "Mounting display SD...", server[128] = "", priority[160] = "", working[160] = "";
};
static Catalog *catalog;
static SemaphoreHandle_t lock;
static std::atomic<bool> scan_requested{true}, local_source{true};
static std::atomic<unsigned> config_revision{0};
static std::atomic<bool> capture_paused{false}, capture_active{false}, preparing{false};
static unsigned file_page = 0;
static unsigned worker_stack_remaining = 0;
static bool valid_path(const char *path) {
    if (!path || *path != '/' || strlen(path) >= 160 || strstr(path, "..") || strchr(path, '\\'))
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 32 || *p >= 127)
            return false;
    return strncmp(path, "/.panel-previews", 16) != 0;
}
static void message(const char *s, int percent = -1) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(catalog->message, sizeof catalog->message, "%s", s);
    catalog->percent = percent;
    xSemaphoreGive(lock);
}
static bool exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && st.st_size > 0;
}
static bool gcode(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext)
        return false;
    for (auto e : {".nc", ".gc", ".gcode", ".ngc", ".ncc", ".tap", ".cnc", ".txt"})
        if (!strcasecmp(ext, e))
            return true;
    return false;
}
static void collect(const char *relative, unsigned depth) {
    char full[224];
    snprintf(full, sizeof full, "%s%s", ROOT, relative);
    DIR *dir = opendir(full);
    if (!dir)
        return;
    while (auto ent = readdir(dir)) {
        if (ent->d_name[0] == '.')
            continue;
        char path[160];
        if (snprintf(path, sizeof path, "%s%s%s", relative, !strcmp(relative, "/") ? "" : "/", ent->d_name) >=
            (int)sizeof path)
            continue;
        snprintf(full, sizeof full, "%s%s", ROOT, path);
        struct stat st;
        if (stat(full, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 4)
                collect(path, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !gcode(path) || !valid_path(path) || st.st_size <= 0 ||
            st.st_size > 512 * 1024 * 1024)
            continue;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (catalog->count < MaxStorageFiles) {
            catalog->order[catalog->count] = catalog->count;
            auto &e = catalog->entries[catalog->count++];
            e = {};
            snprintf(e.file.path, sizeof e.file.path, "%s", path);
            e.file.size = st.st_size;
        }
        xSemaphoreGive(lock);
    }
    closedir(dir);
}
static bool idle() { return !capture_paused.load() && runtime_background_idle(); }
static bool hash_file(const char *path, char hash[65], unsigned revision) {
    uint32_t size;
    FILE *f = storage_open(path, &size);
    if (!f)
        return false;
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        fclose(f);
        return false;
    }
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    bool ok = mbedtls_sha256_starts(&c, 0) == 0;
    size_t n, total = 0;
    unsigned shown = 101;
    while (ok && (n = fread(buffer, 1, 8192, f))) {
        if (!idle() || config_revision.load() != revision) {
            ok = false;
            break;
        }
        ok = mbedtls_sha256_update(&c, buffer, n) == 0;
        total += n;
        unsigned percent = std::min<uint64_t>(100,(uint64_t)total*100/size);
        if (percent != shown) {char text[64];snprintf(text,sizeof text,"Reading file: %u%%",percent);message(text,percent);shown=percent;}
        if (total % 65536 == 0) vTaskDelay(1);
    }
    if (ferror(f)) {
        ok = false;
    }
    unsigned char digest[32];
    if (ok) ok = mbedtls_sha256_finish(&c, digest) == 0;
    mbedtls_sha256_free(&c);
    fclose(f);
    free(buffer);
    if (ok) {
        for (int i = 0; i < 32; i++)
            snprintf(hash + i * 2, 3, "%02x", digest[i]);
    }
    return ok;
}
static esp_http_client_handle_t client(const char *url, esp_http_client_method_t method) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = method;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 2048;
    cfg.disable_auto_redirect = true;
    return esp_http_client_init(&cfg);
}
static int get(const char *url, char *json, size_t capacity, const char *destination = nullptr, unsigned view = 0) {
    auto h = client(url, HTTP_METHOD_GET);
    if (!h)
        return -1;
    FILE *out = nullptr;
    char temp[256];
    int code = -1;
    size_t total = 0;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(h);
        code = esp_http_client_get_status_code(h);
        if (code == 200 && destination && length > 0 && length < 2 * 1024 * 1024) {
            snprintf(temp, sizeof temp, "%s.part", destination);
            out = fopen(temp, "wb");
            if (!out)
                code = -1;
        } else if (destination)
            code = -1;
        char block[2048];
        int n;
        while (code > 0 && (n = esp_http_client_read(h, block, sizeof block)) > 0) {
            if (!idle() || total + (unsigned)n > 2 * 1024 * 1024) {
                code = -1;
                break;
            }
            if (out) {
                if (fwrite(block, 1, n, out) != (size_t)n) {
                    code = -1;
                    break;
                }
            } else if (total + (unsigned)n < capacity) {
                memcpy(json + total, block, n);
                json[total + n] = 0;
            } else {
                code = -1;
                break;
            }
            total += n;
            if (out && length > 0) {
                char text[80];
                unsigned percent = std::min<uint64_t>(100, (uint64_t)total * 100 / length);
                snprintf(text, sizeof text, "Downloading view %u/8: %u%%", view + 1, percent);
                message(text, (view * 100 + percent) / 8);
            }
        }
        if (!esp_http_client_is_complete_data_received(h) || (length >= 0 && total != (size_t)length))
            code = -1;
    }
    if (out) {
        if (fclose(out))
            code = -1;
        if (code == 200 && total) {
            // FAT rename cannot replace an image left by an interrupted cache fill.
            if (exists(destination) && unlink(destination))
                code = -1;
            if (code == 200 && rename(temp, destination))
                code = -1;
        }
        if (code != 200)
            unlink(temp);
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return code;
}
static bool post(const char *url, const char *path, unsigned revision) {
    uint32_t size;
    FILE *file = storage_open(path, &size);
    if (!file)
        return false;
    auto h = client(url, HTTP_METHOD_POST);
    if (!h) {
        fclose(file);
        return false;
    }
    esp_http_client_set_header(h, "Content-Type", "application/octet-stream");
    message("Connecting to preview server...");
    bool ok = esp_http_client_open(h, size) == ESP_OK;
    char *buffer = (char *)heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer)
        ok = false;
    size_t total = 0;
    unsigned shown = 101;
    while (ok && total < size) {
        if (!idle() || config_revision.load() != revision) {
            ok = false;
            break;
        }
        size_t n = fread(buffer, 1, std::min((size_t)8192, (size_t)size - total), file);
        if (!n) {
            ok = false;
            break;
        }
        for (size_t written = 0; written < n;) {
            int k = esp_http_client_write(h, buffer + written, n - written);
            if (k <= 0) {
                ok = false;
                break;
            }
            written += k;
        }
        total += n;
        unsigned percent = std::min<uint64_t>(100,(uint64_t)total*100/size);
        if (percent != shown) {char text[64];snprintf(text,sizeof text,"Uploading file: %u%%",percent);message(text,percent);shown=percent;}
        if (total % 65536 == 0) vTaskDelay(1);
    }
    free(buffer);
    fclose(file);
    if (ok) {
        message("Upload sent; waiting for server...");
        esp_http_client_fetch_headers(h);
        int code = esp_http_client_get_status_code(h);
        ok = code == 202 || code == 200;
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return ok;
}
static bool prepare(Entry entry, const char *server, unsigned revision, bool &unsupported) {
    unsupported = false;
    char dir[176], url[256], json[2048] = {}, path[256];
    message("Checking preview cache...");
    if (!hash_file(entry.file.path, entry.hash, revision))
        return false;
    snprintf(dir, sizeof dir, "%s/v3-%s", CACHE, entry.hash);
    snprintf(path, sizeof path, "%s/complete", dir);
    bool ready = false;
    if (exists(path)) {
        char stored[65] = {};
        FILE *marker = fopen(path, "rb");
        if (marker) {
            ready = fread(stored, 1, 64, marker) == 64 && !strcmp(stored, entry.hash);
            fclose(marker);
        }
        for (unsigned i = 0; ready && i < 8; i++) {
            snprintf(path, sizeof path, "%s/%s.bin", dir, views[i]);
            unsigned char header[12];
            FILE *bitmap = fopen(path, "rb");
            struct stat st;
            ready = bitmap && !stat(path, &st) && st.st_size == 12 + 740 * 328 * 2 &&
                    fread(header, 1, sizeof header, bitmap) == sizeof header &&
                    header[0] == 0x19 && header[1] == 0x12 && header[2] == 0 && header[3] == 0 &&
                    header[4] == 228 && header[5] == 2 && header[6] == 72 && header[7] == 1 &&
                    header[8] == 200 && header[9] == 5;
            if (bitmap)
                fclose(bitmap);
        }
    }
    if (!ready) {
        wifi_bridge_state_t wifi;
        wifi_bridge_snapshot(&wifi);
        if (!*server || !wifi.connected)
            return false;
        snprintf(url, sizeof url, "%s/v3/jobs/%s/manifest.json", server, entry.hash);
        message("Checking server cache...");
        int code = get(url, json, sizeof json);
        if (code == 404) {
            snprintf(url, sizeof url, "%s/v3/jobs/%s", server, entry.hash);
            message("Sending G-code for previews...");
            if (!post(url, entry.file.path, revision))
                return false;
        } else if (code != 200 && code != 202)
            return false;
        bool generated = false;
        message("Waiting for preview server...");
        for (int i = 0; i < 1800; i++) {
            if (!idle() || config_revision.load() != revision)
                return false;
            snprintf(url, sizeof url, "%s/v3/jobs/%s/manifest.json", server, entry.hash);
            *json = 0;
            code = get(url, json, sizeof json);
            if (code == 200 || code == 202) {
                cJSON *root = cJSON_Parse(json);
                const auto status = cJSON_GetObjectItem(root, "status"),
                           hash = cJSON_GetObjectItem(root, "sha256"),
                           version = cJSON_GetObjectItem(root, "renderer");
                generated = cJSON_IsString(status) && !strcmp(status->valuestring, "ready") &&
                            cJSON_IsString(hash) && !strcmp(hash->valuestring, entry.hash) &&
                            cJSON_IsNumber(version) && version->valueint == 3;
                bool error = cJSON_IsString(status) && !strcmp(status->valuestring, "error");
                if (error) {
                    unsupported = true;
                    const auto reason = cJSON_GetObjectItem(root, "error");
                    message(cJSON_IsString(reason) ? reason->valuestring : "Preview unsupported");
                }
                if (code == 202) {
                    const auto phase = cJSON_GetObjectItem(root, "phase"),
                               percent = cJSON_GetObjectItem(root, "percent"),
                               view = cJSON_GetObjectItem(root, "view");
                    char text[96];
                    if (cJSON_IsString(phase) && !strcmp(phase->valuestring, "parsing") && cJSON_IsNumber(percent)) {
                        unsigned p = std::clamp(percent->valueint, 0, 100);
                        snprintf(text, sizeof text, "Reading toolpath on server: %u%%", p);
                        message(text, p);
                    } else if (cJSON_IsString(phase) && !strcmp(phase->valuestring, "rendering") &&
                               cJSON_IsNumber(percent) && cJSON_IsNumber(view)) {
                        unsigned p = std::clamp(percent->valueint, 0, 100), v = std::clamp(view->valueint, 1, 8);
                        snprintf(text, sizeof text, "Rendering view %u/8: %u%%", v, p);
                        message(text, ((v - 1) * 100 + p) / 8);
                    } else message("Generating preview views...");
                }
                cJSON_Delete(root);
                if (generated)
                    break;
                if (error)
                    return false;
            } else if (code != 202)
                return false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!generated)
            return false;
        mkdir(CACHE, 0775);
        mkdir(dir, 0775);
        for (unsigned i = 0; i < 8; i++) {
            if (!idle())
                return false;
            snprintf(url, sizeof url, "%s/v3/jobs/%s/%s.bin", server, entry.hash, views[i]);
            snprintf(path, sizeof path, "%s/%s.bin", dir, views[i]);
            char text[64];
            snprintf(text, sizeof text, "Downloading view %u/8...", i + 1);
            message(text, i * 100 / 8);
            if (get(url, json, sizeof json, path, i) != 200)
                return false;
        }
        snprintf(path, sizeof path, "%s/complete", dir);
        FILE *f = fopen(path, "w");
        if (!f)
            return false;
        bool ok = fputs(entry.hash, f) >= 0;
        ok = fclose(f) == 0 && ok;
        if (!ok)
            return false;
        ready = true;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i = 0; i < catalog->count; i++)
        if (!strcmp(catalog->entries[i].file.path, entry.file.path)) {
            snprintf(catalog->entries[i].hash, 65, "%s", entry.hash);
            catalog->entries[i].ready = ready;
        }
    xSemaphoreGive(lock);
    return true;
}
static void worker(void *) {
    esp_err_t err = bsp_sdcard_mount();
    ESP_LOGI("panel_sd", "Mount: %s", esp_err_to_name(err));
    xSemaphoreTake(lock, portMAX_DELAY);
    catalog->mounted = err == ESP_OK;
    xSemaphoreGive(lock);
    if (err != ESP_OK) {
        message("Display SD unavailable; check card and restart");
        vTaskDelete(nullptr);
        return;
    }
    unsigned last_revision = 0;
    bool was_connected = false;
    uint32_t retried_at = 0;
    for (;;) {
        worker_stack_remaining = uxTaskGetStackHighWaterMark(nullptr);
        if (!idle()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (scan_requested.exchange(false)) {
            message("Reading display SD...");
            xSemaphoreTake(lock, portMAX_DELAY);
            catalog->scanning = true;
            catalog->count = 0;
            file_page = 0;
            xSemaphoreGive(lock);
            collect("/", 0);
            xSemaphoreTake(lock, portMAX_DELAY);
            for (unsigned i = 0; i < catalog->count; i++)
                catalog->order[i] = i;
            std::sort(catalog->order, catalog->order + catalog->count, [](unsigned a, unsigned b) {
                return strcasecmp(catalog->entries[a].file.path, catalog->entries[b].file.path) < 0;
            });
            catalog->scanning = false;
            catalog->revision++;
            xSemaphoreGive(lock);
            file_page = 0;
        }
        wifi_bridge_state_t network;
        wifi_bridge_snapshot(&network);
        bool retry_network =
            network.connected && (!was_connected || xTaskGetTickCount() - retried_at > pdMS_TO_TICKS(60000));
        was_connected = network.connected;
        if (config_revision.load() != last_revision || retry_network) {
            retried_at = xTaskGetTickCount();
            last_revision = config_revision.load();
            for (unsigned i = 0; i < catalog->count; i++)
                catalog->entries[i].tried = false;
        }
        Entry next = {};
        char server[128];
        bool have = false;
        xSemaphoreTake(lock, portMAX_DELAY);
        snprintf(server, sizeof server, "%s", catalog->server);
        for (unsigned i = 0; i < catalog->count; i++) {
            auto &entry = catalog->entries[catalog->order[i]];
            if (entry.ready)
                continue;
            if (*catalog->priority && strcmp(entry.file.path, catalog->priority))
                continue;
            if (!entry.tried) {
                next = entry;
                snprintf(catalog->working, sizeof catalog->working, "%s", entry.file.path);
                have = true;
                break;
            }
        }
        *catalog->priority = 0;
        xSemaphoreGive(lock);
        if (have) {
            preparing = true;
            bool unsupported = false;
            bool ok = idle() && prepare(next, server, last_revision, unsupported);
            preparing = false;
            if (capture_paused.load() && !ok) {
                xSemaphoreTake(lock, portMAX_DELAY);
                if (!*catalog->priority) snprintf(catalog->priority, sizeof catalog->priority, "%s", next.file.path);
                xSemaphoreGive(lock);
            }
            if (idle()) {
                xSemaphoreTake(lock, portMAX_DELAY);
                for (unsigned i = 0; i < catalog->count; i++)
                    if (!strcmp(catalog->entries[i].file.path, next.file.path))
                        catalog->entries[i].tried = true;
                xSemaphoreGive(lock);
            }
            if (ok)
                message("Previews ready");
            else if (idle() && !unsupported)
                message(*server ? "Preview not ready; check server or refresh"
                                : "Set Preview server in Settings to generate images");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
void storage_init() {
    if (lock) {
        return;
    }
    lock = xSemaphoreCreateMutex();
    assert(lock);
    auto memory = heap_caps_malloc(sizeof(Catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(memory);
    catalog = new (memory) Catalog;
    nvs_handle_t h;
    if (nvs_open("panelstorage", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof catalog->server;
        nvs_get_str(h, "server", catalog->server, &n);
        uint8_t local = 1;
        if (nvs_get_u8(h, "gcam_local", &local) == ESP_OK) local_source = local != 0;
        nvs_close(h);
    }
    assert(xTaskCreate(worker, "panel_sd", 12288, nullptr, 1, nullptr) == pdPASS);
}
bool storage_can_upload() {
    if (!catalog) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = catalog->mounted && catalog->count < MaxStorageFiles;
    xSemaphoreGive(lock);
    return ok;
}
bool storage_upload(const char *path, const char *data, size_t size) {
    if (!storage_can_upload() || !valid_path(path) || strchr(path + 1, '/') ||
        !gcode(path) || !data || !size || size > MaxPreviewBytes) return false;
    char full[224];
    snprintf(full, sizeof full, "%s%s", ROOT, path);
    // Exclusive creation preserves every existing SD job, including empty files.
    int fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return false;
    size_t used = 0;
    while (used < size) {
        int n = write(fd, data + used, std::min<size_t>(4096, size - used));
        if (n <= 0) break;
        used += n;
    }
    bool ok = used == size;
    if (fsync(fd)) ok = false;
    if (close(fd)) ok = false;
    if (!ok) unlink(full);
    else storage_scan();
    return ok;
}
void storage_scan() { scan_requested = true; }
bool storage_source(bool local) {
    nvs_handle_t h;
    if (nvs_open("panelstorage", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_u8(h, "gcam_local", local ? 1 : 0) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) local_source = local;
    return ok;
}
bool storage_source() { return local_source.load(); }
void storage_files(State &state) {
    if (!catalog) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    state.file_total = catalog->count;
    state.file_offset = file_page * 4;
    state.file_count = std::min(4u, catalog->count - std::min(catalog->count, file_page * 4));
    state.file_revision = catalog->revision;
    state.listing = scan_requested || catalog->scanning;
    for (int i = 0; i < state.file_count; i++)
        state.files[i] = catalog->entries[catalog->order[file_page * 4 + i]].file;
    xSemaphoreGive(lock);
}
void storage_page(int delta) {
    xSemaphoreTake(lock, portMAX_DELAY);
    file_page = std::clamp<int>((int)file_page + delta, 0, catalog->count ? (catalog->count - 1) / 4 : 0);
    catalog->revision++;
    xSemaphoreGive(lock);
}
FILE *storage_open(const char *path, uint32_t *size) {
    if (!valid_path(path)) {
        return nullptr;
    }
    char full[224];
    snprintf(full, sizeof full, "%s%s", ROOT, path);
    struct stat st;
    if (stat(full, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 512 * 1024 * 1024)
        return nullptr;
    if (size) {
        *size = st.st_size;
    }
    return fopen(full, "rb");
}
bool storage_read_head(const char *path, char *text, size_t capacity) {
    FILE *file = storage_open(path, nullptr);
    if (!file) return false;
    bool ok = file_head(file, text, capacity);
    fclose(file);
    return ok;
}
bool storage_preview(const char *path, unsigned view, char *image, size_t capacity) {
    if (!catalog || view >= 8) {
        return false;
    }
    bool ready = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i = 0; i < catalog->count; i++)
        if (!strcmp(catalog->entries[i].file.path, path) && catalog->entries[i].ready) {
            snprintf(image, capacity, "%s/v3-%s/%s.bin", CACHE, catalog->entries[i].hash, views[view]);
            ready = true;
            break;
        }
    xSemaphoreGive(lock);
    return ready;
}
void storage_prioritize(const char *path) {
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i=0; i<catalog->count; ++i) {
        auto &entry=catalog->entries[i];
        if (!strcmp(entry.file.path,path) && !entry.ready) {
            snprintf(catalog->priority,sizeof catalog->priority,"%s",path);
            entry.tried=false;
            ++config_revision;
            break;
        }
    }
    xSemaphoreGive(lock);
}
bool storage_progress(int *percent, const char *path) {
    if (!catalog) { *percent = -1; return false; }
    xSemaphoreTake(lock, portMAX_DELAY);
    *percent = catalog->percent;
    bool active = ((preparing.load() || capture_active.load()) &&
                   (!path || !strcmp(path, catalog->working))) || (!path && catalog->scanning);
    xSemaphoreGive(lock);
    return active;
}
void storage_status(char *s, size_t n) {
    if (!catalog) {
        snprintf(s, n, "Starting SD...");
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(s, n, "%s", catalog->message);
    xSemaphoreGive(lock);
}
void storage_server(char *s, size_t n) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(s, n, "%s", catalog->server);
    xSemaphoreGive(lock);
}
bool storage_server(const char *url) {
    size_t n = strlen(url);
    if (n >= 128 || (n && strncmp(url, "http://", 7)))
        return false;
    for (const char *p = url; *p; p++)
        if (*p <= 32 || *p >= 127 || *p == '?' || *p == '#' || *p == '@')
            return false;
    nvs_handle_t h;
    if (nvs_open("panelstorage", NVS_READWRITE, &h) != ESP_OK)
        return false;
    bool ok = nvs_set_str(h, "server", url) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) {
        xSemaphoreTake(lock, portMAX_DELAY);
        snprintf(catalog->server, sizeof catalog->server, "%s", url);
        while (n && catalog->server[n - 1] == '/')
            catalog->server[--n] = 0;
        xSemaphoreGive(lock);
        config_revision++;
        storage_scan();
    }
    return ok;
}
} // namespace panel
extern "C" void panel_debug_storage(char *buffer, size_t size) {
    using namespace panel;
    cJSON *root = cJSON_CreateObject();
    xSemaphoreTake(lock, portMAX_DELAY);
    cJSON_AddNumberToObject(root, "internal_free",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "internal_largest",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "file_count", catalog->count);
    cJSON_AddNumberToObject(root, "worker_stack", worker_stack_remaining);
    cJSON_AddNumberToObject(root, "page", file_page);
    cJSON_AddBoolToObject(root, "mounted", catalog->mounted);
    cJSON_AddBoolToObject(root, "local", storage_source());
    cJSON_AddBoolToObject(root, "preparing", preparing.load());
    cJSON_AddNumberToObject(root, "percent", catalog->percent);
    cJSON_AddStringToObject(root, "status", catalog->message);
    cJSON_AddStringToObject(root, "server", catalog->server);
    cJSON *files = cJSON_AddArrayToObject(root, "files");
    for (unsigned i = file_page * 4; i < catalog->count && i < file_page * 4 + 4; i++) {
        auto item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path", catalog->entries[catalog->order[i]].file.path);
        cJSON_AddNumberToObject(item, "size", catalog->entries[catalog->order[i]].file.size);
        cJSON_AddBoolToObject(item, "preview", catalog->entries[catalog->order[i]].ready);
        cJSON_AddItemToArray(files, item);
    }
    xSemaphoreGive(lock);
    cJSON_PrintPreallocated(root, buffer, size, false);
    cJSON_Delete(root);
}
extern "C" bool panel_storage_capture_pause(bool pause) {
    if (pause) panel::capture_active = panel::preparing.load();
    panel::capture_paused = pause;
    if (!pause) { panel::capture_active = false; return true; }
    for (unsigned i=0; i<2000 && panel::preparing.load(); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    return !panel::preparing.load();
}
