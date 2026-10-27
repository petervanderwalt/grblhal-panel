#pragma once
#include "esp_http_server.h"
#include <stdint.h>
namespace panel {
struct WebdavActivity {
    bool active=false, failed=false;
    uint32_t started=0, updated=0, done=0, total=0;
    char action[24]={}, path[160]={};
};
void webdav_init();
void webdav_register(httpd_handle_t server);
bool webdav_enabled();
bool webdav_enabled(bool enabled);
void webdav_activity(WebdavActivity &activity);
bool webdav_recover();
bool webdav_self_test();
}
