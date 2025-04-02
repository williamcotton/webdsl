#include "stringbuilder.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define INITIAL_CAPACITY 1024
#define SIZE_MAX 1024 * 1024

StringBuilder *StringBuilder_new(Arena *arena) {
    if (!arena) return NULL;
    
    StringBuilder *sb = arenaAlloc(arena, sizeof(StringBuilder));
    if (!sb) return NULL;
    
    sb->arena = arena;
    sb->buffer = arenaAlloc(arena, INITIAL_CAPACITY);
    if (!sb->buffer) return NULL;
    
    sb->capacity = INITIAL_CAPACITY;
    sb->length = 0;
    sb->buffer[0] = '\0';
    
    return sb;
}

void StringBuilder_append(StringBuilder *sb, const char *format, ...) {
    if (!sb || !format) return;
    
    va_list args;
    va_start(args, format);

    // Try to print into the existing buffer
    size_t remaining = sb->capacity - sb->length;
    int needed = vsnprintf(sb->buffer + sb->length, remaining, format, args);
    va_end(args);

    if (needed < 0) {
        // Handle encoding error
        return;
    }

    if ((size_t)needed >= remaining) {
        // Buffer wasn't big enough, resize and try again
        size_t new_capacity = sb->capacity;
        size_t min_new_size = sb->length + (size_t)needed + 1;
        
        // Check for integer overflow in new capacity calculation
        while (new_capacity < min_new_size) {
            if (new_capacity > SIZE_MAX / 2) {
                // Would overflow, cap at max possible size
                new_capacity = SIZE_MAX;
                break;
            }
            new_capacity *= 2;
        }
        
        if (new_capacity <= sb->capacity) {
            // Can't expand further
            return;
        }

        char *new_buffer = arenaAlloc(sb->arena, new_capacity);
        if (!new_buffer) return;
        
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
    if (!sb) return NULL;
    return sb->buffer;
}
