#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} Arena;

Arena* createArena(size_t size);
void* arenaAlloc(Arena *arena, size_t size);
char* arenaDupString(Arena *arena, const char *str);
void freeArena(Arena *arena);

#endif // ARENA_H
