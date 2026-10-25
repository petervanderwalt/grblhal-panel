#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

void *__real_memcpy(void *, const void *, size_t);
void *__real_memmove(void *, const void *, size_t);

// Match framebuffer capture's internal staging on this early P4.
static void IRAM_ATTR staged_copy(void *dest, const void *source, size_t size, bool backwards) {
    uint8_t stage[128];
    volatile uint8_t *dst=dest;
    const volatile uint8_t *src=source;
    size_t done=0;
    while(done<size) {
        size_t n=size-done<sizeof stage?size-done:sizeof stage;
        size_t offset=backwards?size-done-n:done;
        for(size_t i=0;i<n;++i)stage[i]=src[offset+i];
        for(size_t i=0;i<n;++i)dst[offset+i]=stage[i];
        done+=n;
    }
}
void *IRAM_ATTR __wrap_memcpy(void *dest,const void *source,size_t size) {
    if(!size)return dest;
    if(esp_ptr_external_ram(dest)||esp_ptr_external_ram(source)) {
        staged_copy(dest,source,size,false);
        return dest;
    }
    return __real_memcpy(dest,source,size);
}
void *IRAM_ATTR __wrap_memmove(void *dest,const void *source,size_t size) {
    if(!size || dest==source)return dest;
    if(esp_ptr_external_ram(dest)||esp_ptr_external_ram(source)) {
        staged_copy(dest,source,size,(uintptr_t)dest>(uintptr_t)source);
        return dest;
    }
    return __real_memmove(dest,source,size);
}
void *__wrap_lv_memcpy(void *, const void *, size_t) __attribute__((alias("__wrap_memcpy")));
void *__wrap_lv_memmove(void *, const void *, size_t) __attribute__((alias("__wrap_memmove")));
bool panel_memory_test(void) {
    uint8_t *a=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *b=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *expected=heap_caps_malloc(4096,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    bool ok=a&&b&&expected;
    if(ok) {
        for(size_t i=0;i<4096;++i)a[i]=expected[i]=(uint8_t)(i*37);
        __wrap_memcpy(expected,a,4096);
        __wrap_memcpy(b,expected,4096);
        for(size_t i=0;i<4096;++i)ok=ok && b[i]==expected[i];
        __wrap_lv_memcpy(b+1,a+3,3001);
        for(size_t i=0;i<3001;++i)ok=ok && b[i+1]==a[i+3];
        __real_memmove(expected+3,expected,3001);
        __wrap_memmove(a+3,a,3001);
        for(size_t i=0;i<4096;++i)ok=ok && a[i]==expected[i];
        __real_memmove(expected,expected+7,3001);
        __wrap_lv_memmove(a,a+7,3001);
        for(size_t i=0;i<4096;++i)ok=ok && a[i]==expected[i];
    }
    free(a);free(b);free(expected);
    printf("@UGTEST %s staged_external_memory_copy_and_overlapping_move\n",ok?"PASS":"FAIL");
    return ok;
}
