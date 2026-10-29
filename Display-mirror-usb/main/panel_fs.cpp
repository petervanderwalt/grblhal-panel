#include "panel_storage.h"
#include "esp_attr.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace panel {
// Only the LVGL task calls this reader; SD DMA always targets internal memory.
static DMA_ATTR __attribute__((aligned(64))) unsigned char read_stage[1024];
bool storage_read_image(const char *path, void *pixels, size_t capacity) {
    static_assert(sizeof(lv_image_header_t)==12, "Native preview header changed");
    static constexpr char prefix[]="/sdcard/.panel-previews/";
    constexpr size_t bytes=740*328*2;
    if (!pixels || capacity<bytes || strncmp(path,prefix,sizeof prefix-1) || strstr(path,".."))
        return false;
    int file=open(path,O_RDONLY);
    if (file<0) return false;
    struct stat info;
    lv_image_header_t header={};
    bool ok=!fstat(file,&info) && info.st_size==12+bytes &&
        read(file,&header,sizeof header)==sizeof header && header.magic==LV_IMAGE_HEADER_MAGIC &&
        header.cf==LV_COLOR_FORMAT_RGB565 && !header.flags && header.w==740 && header.h==328 &&
        header.stride==1480;
    auto dest=(volatile unsigned char *)pixels;
    size_t total=0;
    while(ok && total<bytes) {
        ssize_t n=read(file,read_stage,std::min(sizeof read_stage,bytes-total));
        if(n<=0) {ok=false;break;}
        for(ssize_t i=0;i<n;++i) dest[total+i]=read_stage[i];
        total+=n;
        if(total%65536==0) vTaskDelay(1);
    }
    close(file);
    return ok && total==bytes;
}
} // namespace panel
