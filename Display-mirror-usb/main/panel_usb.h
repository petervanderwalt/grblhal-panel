#pragma once
#include <stddef.h>
#include <stdint.h>
namespace panel {
bool usb_prepare();
bool usb_enable(bool on);
void usb_poll();
bool usb_connected();
uint32_t usb_generation();
bool usb_write(const uint8_t *bytes, size_t size);
size_t usb_read(uint8_t *bytes, size_t size);
void usb_flush();
void usb_status(char *text, size_t size);
}
extern "C" void panel_debug_usb(char *buffer, size_t size);
