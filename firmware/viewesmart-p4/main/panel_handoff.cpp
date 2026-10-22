#include "panel_handoff.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "panel_runtime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
namespace {
static httpd_handle_t server;
static bool cors(httpd_req_t *request) {
    char origin[128] = {};
    size_t n = httpd_req_get_hdr_value_len(request, "Origin");
    if (!n)
        return true;
    if (n >= sizeof origin || httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof origin) != ESP_OK)
        return false;
    const char *allowed[] = {"https://petervanderwalt.github.io",
                             "https://kglovern.github.io",
                             "http://localhost:5174",
                             "http://127.0.0.1:5174",
                             "http://localhost:5180",
                             "http://127.0.0.1:5180"};
    const char *match = nullptr;
    for (auto url : allowed)
        if (!strcmp(url, origin))
            match = url;
    if (!match)
        return false;
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", match);
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(request, "Access-Control-Max-Age", "600");
    httpd_resp_set_hdr(request, "Vary", "Origin");
    return true;
}
static esp_err_t response(httpd_req_t *request, const char *status, const char *body) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}
static bool simple_json(const char *data, size_t size) {
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (size_t i = 0; i < size; i++) {
        char c = data[i];
        if (!c)
            return false;
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        if (!quoted && (c == '{' || c == '[') && ++depth > 8)
            return false;
        if (!quoted && (c == '}' || c == ']')) {
            if (!depth)
                return false;
            depth--;
        }
    }
    return !strstr(data, "\\u0000");
}
static esp_err_t handle(httpd_req_t *request) {
    if (!cors(request))
        return response(request, "403 Forbidden", "{\"error\":\"origin-not-allowed\"}");
    if (request->method == HTTP_OPTIONS)
        return response(request, "204 No Content", "");
    bool ready = panel::runtime_can_upload();
    if (request->method == HTTP_GET) {
        static panel::State state;
        panel::runtime_snapshot(state);
        char json[512];
        if (!strcmp(request->uri, "/gcam/v1/ping")) {
            // gCAM v1 requires this compatibility identifier; product identifies the physical receiver.
            snprintf(json, sizeof json,
                     "{\"app\":\"gSender\",\"product\":\"grblHAL "
                     "panel\",\"version\":\"panel-1\",\"enabled\":true,\"busy\":%s,\"accepting\":%s,"
                     "\"maxBytes\":2097152,\"destination\":\"controller-sd\"}",
                     ready ? "false" : "true", ready ? "true" : "false");
        } else {
            cJSON *j = cJSON_CreateObject();
            if (!j)
                return response(request, "500 Internal Server Error", "{\"error\":\"memory\"}");
            cJSON_AddBoolToObject(j, "uploading", state.uploading);
            cJSON_AddBoolToObject(j, "success", state.upload_success);
            cJSON_AddNumberToObject(j, "bytes", state.upload_bytes);
            cJSON_AddNumberToObject(j, "total", state.upload_total);
            cJSON_AddStringToObject(j, "path", state.upload_path);
            cJSON_AddStringToObject(j, "message", state.message);
            bool ok = cJSON_PrintPreallocated(j, json, sizeof json, false);
            cJSON_Delete(j);
            if (!ok)
                return response(request, "500 Internal Server Error", "{\"error\":\"memory\"}");
        }
        return response(request, "200 OK", json);
    }
    char type[64] = {};
    httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof type);
    if (strncasecmp(type, "application/json", 16) ||
        (type[16] && type[16] != ';' && !isspace((unsigned char)type[16])))
        return response(request, "415 Unsupported Media Type", "{\"error\":\"expected-json\"}");
    if (!request->content_len)
        return response(request, "400 Bad Request", "{\"error\":\"invalid-json\"}");
    if (request->content_len > 3 * 1024 * 1024)
        return response(request, "413 Payload Too Large", "{\"error\":\"too-large\"}");
    char *body = (char *)heap_caps_malloc(request->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body)
        return response(request, "503 Service Unavailable", "{\"error\":\"memory\"}");
    size_t used = 0;
    while (used < request->content_len) {
        int n = httpd_req_recv(request, body + used, request->content_len - used);
        if (n <= 0) {
            free(body);
            return response(request, "408 Request Timeout", "{\"error\":\"receive-timeout\"}");
        }
        used += n;
    }
    body[used] = 0;
    cJSON *json =
        simple_json(body, used) ? cJSON_ParseWithLengthOpts(body, used + 1, nullptr, true) : nullptr;
    free(body);
    if (!json || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return response(request, "400 Bad Request", "{\"error\":\"invalid-json\"}");
    }
    cJSON *code = cJSON_GetObjectItemCaseSensitive(json, "gcode");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "name");
    if (!cJSON_IsString(code) || !code->valuestring) {
        cJSON_Delete(json);
        return response(request, "400 Bad Request", "{\"error\":\"missing-gcode\"}");
    }
    size_t size = strlen(code->valuestring);
    bool meaningful = false;
    for (size_t i = 0; i < size; i++)
        if (!isspace((unsigned char)code->valuestring[i]))
            meaningful = true;
    if (!meaningful) {
        cJSON_Delete(json);
        return response(request, "400 Bad Request", "{\"error\":\"missing-gcode\"}");
    }
    if (size > panel::MaxPreviewBytes) {
        cJSON_Delete(json);
        return response(request, "413 Payload Too Large", "{\"error\":\"too-large\"}");
    }
    if (!ready) {
        cJSON_Delete(json);
        return response(request, "409 Conflict", "{\"error\":\"controller-not-ready\"}");
    }
    char filename[101], path[102];
    panel_handoff_name(cJSON_IsString(name) ? name->valuestring : nullptr, filename, sizeof filename);
    snprintf(path, sizeof path, "/%s", filename);
    bool accepted = panel::runtime_upload(path, code->valuestring, size);
    cJSON_Delete(json);
    if (!accepted)
        return response(request, "409 Conflict", "{\"error\":\"controller-not-ready\"}");
    char result[256];
    snprintf(result, sizeof result,
             "{\"accepted\":true,\"name\":\"%s\",\"destination\":\"controller-sd\",\"status\":\"/gcam/v1/"
             "status\"}",
             filename);
    return response(request, "202 Accepted", result);
}
} // namespace
extern "C" void panel_handoff_name(const char *name, char *output, size_t size) {
    if (size < 8) {
        if (size)
            output[0] = 0;
        return;
    }
    size_t count = 0, limit = size - 4;
    if (limit > 96)
        limit = 96;
    if (name)
        for (const unsigned char *p = (const unsigned char *)name; *p && count < limit; p++) {
            char c = *p;
            if (!count && (c == '.' || isspace(*p)))
                continue;
            output[count++] = *p < 32 || *p >= 127 || strchr("/\\<>:\"|?*!~]", c) ? '_' : c;
        }
    while (count && isspace((unsigned char)output[count - 1]))
        count--;
    output[count] = 0;
    if (!count)
        snprintf(output, size, "gcam");
    const char *ext = strrchr(output, '.');
    if (!ext || (strcasecmp(ext, ".nc") && strcasecmp(ext, ".gcode") && strcasecmp(ext, ".gc") &&
                 strcasecmp(ext, ".tap") && strcasecmp(ext, ".cnc")))
        strncat(output, ".nc", size - strlen(output) - 1);
}
extern "C" void panel_handoff_start(void) {
    if (server)
        return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 47820;
    config.stack_size = 8192;
    config.max_uri_handlers = 7;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    if (httpd_start(&server, &config) != ESP_OK) {
        server = nullptr;
        ESP_LOGE("handoff", "HTTP listener failed");
        return;
    }
    const char *paths[] = {"/gcam/v1/ping", "/gcam/v1/load", "/gcam/v1/status"};
    for (auto path : paths) {
        httpd_uri_t options = {};
        options.uri = path;
        options.method = HTTP_OPTIONS;
        options.handler = handle;
        httpd_register_uri_handler(server, &options);
        httpd_uri_t route = {};
        route.uri = path;
        route.method = !strcmp(path, "/gcam/v1/load") ? HTTP_POST : HTTP_GET;
        route.handler = handle;
        httpd_register_uri_handler(server, &route);
    }
}
