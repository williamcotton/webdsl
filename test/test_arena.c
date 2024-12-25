#include "../test/unity/unity.h"
#include "../src/arena.h"
#include "test_runners.h"
#include <string.h>

// Function prototype
int run_arena_tests(void);

static void test_arena_create(void) {
    Arena *arena = createArena(1024);
    
    TEST_ASSERT_NOT_NULL(arena);
    TEST_ASSERT_NOT_NULL(arena->buffer);
    TEST_ASSERT_EQUAL(1024, arena->size);
    TEST_ASSERT_EQUAL(0, arena->used);
    
    freeArena(arena);
}

static void test_arena_alloc(void) {
    Arena *arena = createArena(1024);
    
    // Test basic allocation
    int *num = arenaAlloc(arena, sizeof(int));
    TEST_ASSERT_NOT_NULL(num);
    *num = 42;
    TEST_ASSERT_EQUAL(42, *num);
    
    // Test alignment (should be 8-byte aligned)
    char *byte = arenaAlloc(arena, 1);
    TEST_ASSERT_EQUAL(0, (uintptr_t)byte % 8);
    
    // Test multiple allocations
    int *nums = arenaAlloc(arena, 5 * sizeof(int));
    TEST_ASSERT_NOT_NULL(nums);
    for (int i = 0; i < 5; i++) {
        nums[i] = i;
    }
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i, nums[i]);
    }
    
    freeArena(arena);
}

static void test_arena_string_dup(void) {
    Arena *arena = createArena(1024);
    
    const char *original = "Hello, World!";
    char *copy = arenaDupString(arena, original);
    
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_NOT_EQUAL(original, copy);  // Should be different pointers
    TEST_ASSERT_EQUAL_STRING(original, copy);
    
    // Test empty string
    const char *empty = "";
    char *empty_copy = arenaDupString(arena, empty);
    TEST_ASSERT_NOT_NULL(empty_copy);
    TEST_ASSERT_EQUAL_STRING(empty, empty_copy);
    
    freeArena(arena);
}

static void test_arena_out_of_memory(void) {
    Arena *arena = createArena(16);  // Small arena
    
    // This should work
    void *ptr1 = arenaAlloc(arena, 8);
    TEST_ASSERT_NOT_NULL(ptr1);
    
    // This should fail due to out of memory
    void *ptr2 = arenaAlloc(arena, 16);
    TEST_ASSERT_NULL(ptr2);
    
    freeArena(arena);
}

static void test_arena_alignment(void) {
    Arena *arena = createArena(1024);
    
    // Allocate a single byte to offset the arena
    char *byte = arenaAlloc(arena, 1);
    TEST_ASSERT_NOT_NULL(byte);
    
    // Next allocation should still be 8-byte aligned
    void *ptr = arenaAlloc(arena, sizeof(double));
    TEST_ASSERT_EQUAL(0, (uintptr_t)ptr % 8);
    
    // Test with various sizes
    size_t sizes[] = {1, 3, 7, 8, 9, 15, 16};
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        void *p = arenaAlloc(arena, sizes[i]);
        TEST_ASSERT_EQUAL(0, (uintptr_t)p % 8);
    }
    
    freeArena(arena);
}

int run_arena_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_arena_create);
    RUN_TEST(test_arena_alloc);
    RUN_TEST(test_arena_string_dup);
    RUN_TEST(test_arena_out_of_memory);
    RUN_TEST(test_arena_alignment);
    return UNITY_END();
}
