#include "panel_stream.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace panel {
void Stream::cancel() {
    if (file_)
        fclose(file_);
    file_ = nullptr;
    free(read_buffer_);
    read_buffer_ = nullptr;
    head_ = count_ = used_ = 0;
    checking_ = eof_ = have_ = false;
}
void Stream::fail(const char *message) {
    cancel();
    snprintf(error_, sizeof error_, "%s", message);
}
bool Stream::start(FILE *file, uint32_t size, uint32_t now) {
    if (active() || !file || !size)
        return false;
#ifdef ESP_PLATFORM
    read_buffer_ = (unsigned char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    read_buffer_ = (unsigned char *)malloc(4096);
#endif
    if (!read_buffer_)
        return false;
    read_at_ = read_size_ = consumed_ = 0;
    file_ = file;
    size_ = size;
    ack_bytes_ = 0;
    stats_ = StreamStats();
    sent_ = now;
    *error_ = 0;
    head_ = count_ = used_ = 0;
    checking_ = true;
    executable_ = false;
    eof_ = have_ = false;
    return true;
}
int Stream::byte() {
    if (read_at_ == read_size_) {
        read_size_ = fread(read_buffer_, 1, 4096, file_);
        read_at_ = 0;
        if (!read_size_)
            return EOF;
    }
    consumed_++;
    return read_buffer_[read_at_++];
}
int Stream::next_line() {
    unsigned n = 0, raw = 0;
    bool comment = false, semicolon = false, seen = false;
    for (;;) {
        int c = byte();
        if (c == EOF) {
            if (ferror(file_) || consumed_ != size_) {
                fail("SD read failed; job stopped");
                return -1;
            }
            if (!seen)
                return 0;
            break;
        }
        seen = true;
        if (c == '\n')
            break;
        if (++raw > 4096) {
            fail("G-code line exceeds 4096 bytes");
            return -1;
        }
        if (c == '\r' || c == '\t')
            continue;
        if (semicolon)
            continue;
        if (c == ';' && !comment) {
            semicolon = true;
            continue;
        }
        if (c == '(') {
            if (comment) {
                fail("Nested G-code comment");
                return -1;
            }
            comment = true;
            continue;
        }
        if (c == ')') {
            if (!comment) {
                fail("Unmatched G-code comment");
                return -1;
            }
            comment = false;
            continue;
        }
        if (comment || c == ' ')
            continue;
        if (c < 32 || c >= 127 || c == '!' || c == '~' || c == '?' || c == '$') {
            fail("G-code contains a realtime/system character");
            return -1;
        }
        if (n + 2 >= sizeof next_) {
            fail("G-code block is too long for the 128-byte RX window");
            return -1;
        }
        next_[n++] = (char)toupper(c);
    }
    if (comment) {
        fail("Unclosed G-code comment");
        return -1;
    }
    next_[n] = 0;
    if (!strcmp(next_, "%"))
        *next_ = 0;
    next_offset_ = consumed_;
    return 1;
}
void Stream::tick(uint32_t now, bool allow) {
    if (!file_)
        return;
    if (checking_) {
        // Validate the complete file before the first motion command, in bounded batches.
        for (int i = 0; i < 128; i++) {
            int r = next_line();
            if (r < 0)
                return;
            if (r && *next_)
                executable_ = true;
            if (!r) {
                if (!executable_) {
                    fail("No G-code commands in file");
                    return;
                }
                if (fseek(file_, 0, SEEK_SET)) {
                    fail("SD seek failed");
                    return;
                }
                clearerr(file_);
                read_at_ = read_size_ = consumed_ = 0;
                checking_ = false;
                sent_ = now;
                return;
            }
        }
        return;
    }
    if (!allow)
        return;
    if (count_ && now - sent_ > 300000) {
        fail("G-code acknowledgement timed out");
        return;
    }
    for (int i = 0; i < 64 && file_; i++) {
        if (!have_ && !eof_) {
            int r = next_line();
            if (r < 0)
                return;
            if (!r) {
                eof_ = true;
                break;
            }
            have_ = *next_ != 0;
            if (!have_)
                continue;
        }
        if (!have_ || count_ >= 64)
            return;
        unsigned length = (unsigned)strlen(next_) + 1;
        if (used_ + length >= 128)
            return;
        if (!send_(context_, next_)) {
            fail("UART write failed; job stopped");
            return;
        }
        if (!count_)
            sent_ = now;
        pending_[(head_ + count_) % 64] = {length, next_offset_};
        count_++;
        used_ += length;
        stats_.sent_lines++; stats_.sent_bytes+=length;
        if (used_>stats_.high_water) stats_.high_water=used_;
        for (const unsigned char *p=(const unsigned char *)next_; *p; ++p) stats_.hash=(stats_.hash^*p)*16777619u;
        stats_.hash=(stats_.hash^10u)*16777619u;
        have_ = false;
    }
}
void Stream::acknowledge(uint32_t now) {
    if (!file_ || !count_)
        return;
    stats_.ack_lines++; stats_.ack_bytes+=pending_[head_].length;
    used_ -= pending_[head_].length;
    ack_bytes_ = pending_[head_].offset;
    head_ = (head_ + 1) % 64;
    count_--;
    sent_ = now;
}
} // namespace panel
