#pragma once
#include <stddef.h>
#include <stdint.h>
namespace panel {
class Upload {
  public:
    using Write = bool (*)(void *, const uint8_t *, size_t);
    Upload(Write write, void *context) : write_(write), context_(context) {}
    bool start(const char *path, const char *data, size_t size, uint32_t now);
    void receive(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    void cancel(const char *reason);
    bool active() const { return phase_ >= Header && phase_ <= Final; }
    bool success() const { return phase_ == Done; }
    size_t transferred() const { return offset_; }
    const char *message() const { return message_; }
    static uint16_t crc(const uint8_t *data, size_t size);

  private:
    enum Phase { Idle, Header, HeaderC, Data, End, EndC, Final, Done, Failed } phase_ = Idle;
    Write write_;
    void *context_;
    const char *data_ = nullptr, *message_ = "";
    size_t size_ = 0, offset_ = 0, packet_size_ = 0;
    uint8_t packet_[1029] = {}, sequence_ = 0;
    unsigned retries_ = 0;
    uint32_t sent_ = 0;
    void send(uint32_t now);
    void packet(size_t length, uint32_t now);
    void next(uint32_t now);
};
bool upload_self_test();
} // namespace panel
