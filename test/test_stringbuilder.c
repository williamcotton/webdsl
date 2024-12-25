#include "../test/unity/unity.h"
#include "../src/stringbuilder.h"
#include "../src/arena.h"
#include <string.h>
#include "test_runners.h"

static void test_stringbuilder_append(void) {
    Arena *arena = createArena(4096);
    TEST_ASSERT_NOT_NULL(arena);
    
    StringBuilder *sb = StringBuilder_new(arena);
    TEST_ASSERT_NOT_NULL(sb);
    
    StringBuilder_append(sb, "Hello");
    TEST_ASSERT_EQUAL_STRING("Hello", StringBuilder_get(sb));
    
    StringBuilder_append(sb, " World");
    TEST_ASSERT_EQUAL_STRING("Hello World", StringBuilder_get(sb));
    
    freeArena(arena);
}

static void test_stringbuilder_format(void) {
    Arena *arena = createArena(4096);
    TEST_ASSERT_NOT_NULL(arena);
    
    StringBuilder *sb = StringBuilder_new(arena);
    TEST_ASSERT_NOT_NULL(sb);
    
    StringBuilder_append(sb, "%d %s", 42, "test");
    TEST_ASSERT_EQUAL_STRING("42 test", StringBuilder_get(sb));
    
    freeArena(arena);
}

static void test_stringbuilder_large_string(void) {
    Arena *arena = createArena(1024 * 64);
    TEST_ASSERT_NOT_NULL(arena);
    
    StringBuilder *sb = StringBuilder_new(arena);
    TEST_ASSERT_NOT_NULL(sb);
    
    for (int i = 0; i < 1000; i++) {
        StringBuilder_append(sb, "test");
    }
    
    TEST_ASSERT_EQUAL(4000, strlen(StringBuilder_get(sb)));
    
    freeArena(arena);
}

int run_stringbuilder_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stringbuilder_append);
    RUN_TEST(test_stringbuilder_format);
    RUN_TEST(test_stringbuilder_large_string);
    return UNITY_END();
}
