#ifndef STRINGBUILDER_H
#define STRINGBUILDER_H

#include "arena.h"

typedef struct StringBuilder {
    char *buffer;
    size_t capacity;
    size_t length;
    Arena *arena;
} StringBuilder;

StringBuilder *StringBuilder_new(Arena *arena);
void StringBuilder_append(StringBuilder *sb, const char *format, ...);
char *StringBuilder_get(StringBuilder *sb);

#endif
