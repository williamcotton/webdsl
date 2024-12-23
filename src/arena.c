#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Arena* createArena(size_t size) {
    Arena *arena = malloc(sizeof(Arena));
    arena->buffer = malloc(size);
    arena->size = size;
    arena->used = 0;
    return arena;
}

void* arenaAlloc(Arena *arena, size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~((size_t)7);
    
    if (arena->used + size > arena->size) {
        fputs("Arena out of memory\n", stderr);
        exit(1);
    }
    
    void *ptr = arena->buffer + arena->used;
    arena->used += size;
    return ptr;
}

char* arenaDupString(Arena *arena, const char *str) {
    size_t len = strlen(str) + 1;
    char *dup = arenaAlloc(arena, len);
    memcpy(dup, str, len);
    return dup;
}

void freeArena(Arena *arena) {
    free(arena->buffer);
    free(arena);
}
