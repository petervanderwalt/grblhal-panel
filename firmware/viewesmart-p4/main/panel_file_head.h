#pragma once
#include <stdio.h>
#include <stddef.h>
namespace panel {
// A bounded, read-only first screen; consume long lines without wrapping them.
inline bool file_head(FILE *file, char *text, size_t capacity) {
    if (!file || !text || capacity < 2) return false;
    size_t used = 0, column = 0, line_start = 0;
    unsigned lines = 0;
    bool clipped = false;
    for (unsigned read = 0; read < 4096 && lines < 14; ++read) {
        int c = fgetc(file);
        if (c == EOF) break;
        if (c == '\r') continue;
        if (c == '\n') {
            if (used + 1 >= capacity) break;
            text[used++] = '\n';
            ++lines; column = 0; line_start = used; clipped = false;
        } else if (column < 80) {
            if (used + 1 >= capacity) break;
            text[used++] = c == '\t' ? ' ' : c >= 32 && c <= 126 ? (char)c : '?';
            ++column;
        } else if (!clipped) {
            if (used >= line_start + 3) text[used-3] = text[used-2] = text[used-1] = '.';
            clipped = true;
        }
    }
    if (used && text[used-1] == '\n') --used;
    text[used] = 0;
    return !ferror(file);
}
}
