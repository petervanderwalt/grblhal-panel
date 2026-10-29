#include "panel_upload.h"
#include <stdio.h>
#include <string.h>
namespace panel {
uint16_t Upload::crc(const uint8_t *data, size_t size) {
    uint16_t crc = 0;
    while (size--) {
        crc ^= (uint16_t)*data++ << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
void Upload::cancel(const char *reason) {
    if (active()) {
        const uint8_t cancel[] = {0x18, 0x18};
        write_(context_, cancel, sizeof cancel);
    }
    phase_ = Failed;
    message_ = reason;
}
void Upload::send(uint32_t now) {
    sent_ = now;
    if (!write_(context_, packet_, packet_size_))
        cancel("SD upload: UART write failed");
}
void Upload::packet(size_t length, uint32_t now) {
    packet_[0] = length == 128 ? 1 : 2;
    packet_[1] = sequence_;
    packet_[2] = ~sequence_;
    uint16_t checksum = crc(packet_ + 3, length);
    packet_[3 + length] = checksum >> 8;
    packet_[4 + length] = checksum;
    packet_size_ = length + 5;
    retries_ = 0;
    send(now);
}
bool Upload::start(const char *path, const char *data, size_t size, uint32_t now) {
    if (active() || !path || path[0] != '/' || strlen(path) > 100 || !data || !size || size > 2 * 1024 * 1024)
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 32 || *p >= 127 || strchr("!~?|]", *p))
            return false;
    data_ = data;
    size_ = size;
    offset_ = 0;
    sequence_ = 0;
    phase_ = Header;
    message_ = "Uploading to controller SD";
    memset(packet_ + 3, 0, 128);
    snprintf((char *)packet_ + 3, 128, "%s", path);
    snprintf((char *)packet_ + 4 + strlen(path), 127 - strlen(path), "%u", (unsigned)size);
    packet(128, now);
    return active();
}
void Upload::next(uint32_t now) {
    memset(packet_ + 3, 0x1A, 1024);
    size_t count = size_ - offset_;
    if (count > 1024)
        count = 1024;
    memcpy(packet_ + 3, data_ + offset_, count);
    phase_ = Data;
    packet(1024, now);
}
void Upload::receive(uint8_t byte, uint32_t now) {
    if (!active())
        return;
    if (byte == 0x18) {
        cancel("Controller cancelled SD upload");
        return;
    }
    if (byte == 0x15) {
        if (++retries_ > 10)
            cancel("SD upload: too many retries");
        else
            send(now);
        return;
    }
    if (phase_ == HeaderC && byte == 'C') {
        sequence_ = 1;
        next(now);
    } else if (phase_ == Header && byte == 6) {
        phase_ = HeaderC;
        sent_ = now;
    } else if (phase_ == Data && byte == 6) {
        offset_ += size_ - offset_ < 1024 ? size_ - offset_ : 1024;
        sequence_++;
        if (offset_ < size_)
            next(now);
        else {
            phase_ = End;
            packet_[0] = 4;
            packet_size_ = 1;
            retries_ = 0;
            send(now);
        }
    } else if (phase_ == End && byte == 6) {
        phase_ = EndC;
        sent_ = now;
    } else if (phase_ == EndC && byte == 'C') {
        phase_ = Final;
        sequence_ = 0;
        memset(packet_ + 3, 0, 128);
        packet(128, now);
    } else if (phase_ == Final && byte == 6) {
        phase_ = Done;
        message_ = "Uploaded to controller SD";
    }
}
void Upload::tick(uint32_t now) {
    if (active() && now - sent_ > 3000) {
        if (++retries_ > 10)
            cancel("SD upload timed out");
        else
            send(now);
    }
}
} // namespace panel
