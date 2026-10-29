#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
namespace panel {
// Same conservative character-counting window as Ooznest WebSerial, including LF.
bool stream_self_test();
struct StreamStats {
    uint32_t sent_lines=0, ack_lines=0, sent_bytes=0, ack_bytes=0, hash=2166136261u;
    unsigned high_water=0;
};
class Stream {
  public:
    using Send = bool (*)(void *, const char *);
    Stream(Send send, void *context) : send_(send), context_(context) {}
    bool start(FILE *file, uint32_t size, uint32_t now);
    void tick(uint32_t now, bool allow);
    void acknowledge(uint32_t now);
    void cancel();
    bool active() const { return file_ != nullptr; }
    bool done() const { return eof_ && count_ == 0 && !checking_; }
    bool checking() const { return checking_; }
    bool failed() const { return *error_; }
    const char *error() const { return error_; }
    uint32_t acknowledged() const { return ack_bytes_; }
    uint32_t sent_at() const { return sent_; }
    unsigned outstanding() const { return count_; }
    unsigned in_flight() const { return used_; }
    const StreamStats &stats() const { return stats_; }
    uint32_t checked_bytes() const { return consumed_; }

  private:
    StreamStats stats_;
    Send send_;
    void *context_;
    FILE *file_ = nullptr;
    uint32_t size_ = 0, ack_bytes_ = 0, sent_ = 0;
    struct Pending {
        unsigned length;
        uint32_t offset;
    } pending_[64];
    unsigned head_ = 0, count_ = 0, used_ = 0;
    bool checking_ = false, eof_ = false, have_ = false, executable_ = false;
    char next_[128] = {}, error_[96] = {};
    uint32_t next_offset_ = 0, consumed_ = 0;
    unsigned char *read_buffer_ = nullptr;
    size_t read_at_ = 0, read_size_ = 0;
    int byte();
    int next_line();
    void fail(const char *message);
};
} // namespace panel
