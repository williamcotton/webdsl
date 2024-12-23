#include "stringbuilder.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define INITIAL_CAPACITY 1024

StringBuilder *StringBuilder_new(Arena *arena) {
    StringBuilder *sb = arenaAlloc(arena, sizeof(StringBuilder));
    sb->buffer = arenaAlloc(arena, INITIAL_CAPACITY);
    sb->capacity = INITIAL_CAPACITY;
    sb->length = 0;
    sb->arena = arena;
    sb->buffer[0] = '\0';
    return sb;
}

void StringBuilder_append(StringBuilder *sb, const char *format, ...) {
    va_list args;
    va_start(args, format);

    // Try to print into the existing buffer
    size_t remaining = sb->capacity - sb->length;
    int needed = vsnprintf(sb->buffer + sb->length, remaining, format, args);
    va_end(args);

    if ((size_t)needed >= remaining) {
        // Buffer wasn't big enough, resize and try again
        size_t new_capacity = sb->capacity;
        while ((size_t)needed >= (new_capacity - sb->length)) {
            new_capacity *= 2;
        }

        char *new_buffer = arenaAlloc(sb->arena, new_capacity);
        memcpy(new_buffer, sb->buffer, sb->length);
        sb->buffer = new_buffer;
        sb->capacity = new_capacity;

        va_start(args, format);
        vsnprintf(sb->buffer + sb->length, sb->capacity - sb->length, format, args);
        va_end(args);
    }

    sb->length += (size_t)needed;
}

char *StringBuilder_get(StringBuilder *sb) {
    return sb->buffer;
}
