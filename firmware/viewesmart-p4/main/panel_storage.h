#pragma once
#include "panel_model.h"
namespace panel {
void storage_init();
bool storage_read_head(const char *path, char *text, size_t capacity);
bool storage_read_image(const char *path, void *pixels, size_t capacity);
void storage_scan();
bool storage_update_start();
void storage_update_cancel();
void storage_update_state(bool &active, bool &busy, unsigned &done, unsigned &total, unsigned &failed);
bool storage_can_upload();
bool storage_upload(const char *path, const char *data, size_t size);
void storage_page(int delta);
void storage_files(State &state);
bool storage_source();
bool storage_source(bool local);
FILE *storage_open(const char *path, uint32_t *size);
bool storage_preview(const char *path, unsigned view, char *image, size_t capacity);
void storage_preview_viewing(bool viewing);
void storage_lookup_preview(const char *path);
void storage_status(char *text, size_t size, char *working = nullptr, size_t working_size = 0);
bool storage_progress(int *percent, const char *path = nullptr);
void storage_server(char *url, size_t size);
bool storage_server(const char *url);
bool runtime_background_idle();
} // namespace panel
extern "C" void panel_debug_storage(char *, size_t);
