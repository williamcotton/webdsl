#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#ifdef USE_VALGRIND
#include <valgrind/memcheck.h>
#else
#define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed) ((void)0)
#define VALGRIND_DESTROY_MEMPOOL(pool) ((void)0)
#define VALGRIND_MEMPOOL_ALLOC(pool, addr, size) ((void)0)
#define VALGRIND_MEMPOOL_FREE(pool, addr) ((void)0)
#define VALGRIND_MAKE_MEM_NOACCESS(addr, size) ((void)0)
#define VALGRIND_MAKE_MEM_UNDEFINED(addr, size) ((void)0)
#define VALGRIND_MAKE_MEM_DEFINED(addr, size) ((void)0)
#endif

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
