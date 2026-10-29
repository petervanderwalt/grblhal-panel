#include "panel_upload.h"
#include <stdio.h>
#include <string.h>
namespace panel {
namespace {
struct Capture {
    uint8_t packet[1029];
    size_t size = 0;
    unsigned count = 0;
    bool fail = false;
};
bool write(void *context, const uint8_t *data, size_t size) {
    Capture &c = *(Capture *)context;
    memcpy(c.packet, data, size);
    c.size = size;
    c.count++;
    return !c.fail;
}
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            printf("@UGTEST FAIL upload:%d\n", __LINE__);                                                    \
            return false;                                                                                    \
        }                                                                                                    \
    } while (0)
} // namespace
bool upload_self_test() {
    Capture c = {};
    Upload upload(write, &c);
    char data[1100];
    memset(data, 'G', sizeof data);
    CHECK(Upload::crc((const uint8_t *)"123456789", 9) == 0x31C3);
    CHECK(!upload.start("../bad", data, sizeof data, 0));
    CHECK(upload.start("/part.nc", data, sizeof data, 0));
    CHECK(c.size == 133 && c.packet[0] == 1 && !strcmp((char *)c.packet + 3, "/part.nc"));
    CHECK(!strcmp((char *)c.packet + 12, "1100"));
    upload.receive(6, 1);
    upload.receive('C', 2);
    CHECK(c.size == 1029 && c.packet[1] == 1 && c.packet[1026] == 'G');
    upload.receive(0x15, 3);
    CHECK(c.packet[1] == 1 && upload.transferred() == 0);
    upload.receive(6, 4);
    CHECK(c.packet[1] == 2 && c.packet[3 + 76] == 0x1A && upload.transferred() == 1024);
    upload.receive(6, 5);
    CHECK(c.size == 1 && c.packet[0] == 4 && upload.transferred() == sizeof data);
    upload.receive(0x15, 6);
    CHECK(c.packet[0] == 4);
    upload.receive(6, 7);
    upload.receive('C', 8);
    CHECK(c.size == 133 && c.packet[1] == 0 && c.packet[3] == 0);
    upload.receive(6, 9);
    CHECK(upload.success() && !upload.active());
    CHECK(upload.start("/part.nc", data, sizeof data, 10));
    upload.receive(0x18, 11);
    CHECK(!upload.active() && !upload.success());
    CHECK(upload.start("/part.nc", data, sizeof data, 20));
    for (unsigned i = 1; i <= 11; i++)
        upload.tick(20 + i * 3001);
    CHECK(!upload.active() && !upload.success());
    c.fail = true;
    CHECK(!upload.start("/part.nc", data, sizeof data, 40000));
    printf("@UGTEST PASS sd_upload_crc_retry_cancel_timeout\n");
    return true;
}
} // namespace panel
