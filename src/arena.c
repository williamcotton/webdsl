#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Arena* createArena(size_t size) {
    Arena *arena = malloc(sizeof(Arena));
    if (arena == NULL) {
        return NULL;
    }
    
    arena->buffer = malloc(size);
    if (arena->buffer == NULL) {
        free(arena);
        return NULL;
    }
    
    arena->size = size;
    arena->used = 0;
    
    // Register the arena as a memory pool with Valgrind
    VALGRIND_CREATE_MEMPOOL(arena, 0, 0);
    // Mark the entire buffer as noaccess initially
    VALGRIND_MAKE_MEM_NOACCESS(arena->buffer, size);
    
    return arena;
}

void* arenaAlloc(Arena *arena, size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~((size_t)7);
    
    if (arena->used + size > arena->size) {
        return NULL;
    }
    
    void *ptr = arena->buffer + arena->used;
    arena->used += size;
    
    // Tell Valgrind this memory is now allocated and undefined
    VALGRIND_MEMPOOL_ALLOC(arena, ptr, size);
    VALGRIND_MAKE_MEM_UNDEFINED(ptr, size);
    
    return ptr;
}

char* arenaDupString(Arena *arena, const char *str) {
    size_t len = strlen(str) + 1;
    char *dup = arenaAlloc(arena, len);
    if (dup) {
        memcpy(dup, str, len);
        // Tell Valgrind this memory is now defined
        VALGRIND_MAKE_MEM_DEFINED(dup, len);
    }
    return dup;
}

void freeArena(Arena *arena) {
    // Tell Valgrind we're freeing the entire pool
    VALGRIND_DESTROY_MEMPOOL(arena);
    free(arena->buffer);
    free(arena);
}
