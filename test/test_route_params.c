#include "../test/unity/unity.h"
#include "../src/server/route_params.h"
#include "test_runners.h"
#include <string.h>

// Function prototype
int run_route_params_tests(void);

static void test_route_params_basic(void) {
    Arena *arena = createArena(1024);
    RouteParams params;
    
    bool matches = parseRouteParams("/users/:id", "/users/123", &params, arena);
    
    TEST_ASSERT_TRUE(matches);
    TEST_ASSERT_EQUAL(1, params.count);
    TEST_ASSERT_EQUAL_STRING("id", params.params[0].name);
    TEST_ASSERT_EQUAL_STRING("123", params.params[0].value);
    
    freeArena(arena);
}

static void test_route_params_multiple(void) {
    Arena *arena = createArena(1024);
    RouteParams params;
    
    bool matches = parseRouteParams("/users/:id/posts/:post_id", "/users/123/posts/456", &params, arena);
    
    TEST_ASSERT_TRUE(matches);
    TEST_ASSERT_EQUAL(2, params.count);
    TEST_ASSERT_EQUAL_STRING("id", params.params[0].name);
    TEST_ASSERT_EQUAL_STRING("123", params.params[0].value);
    TEST_ASSERT_EQUAL_STRING("post_id", params.params[1].name);
    TEST_ASSERT_EQUAL_STRING("456", params.params[1].value);
    
    freeArena(arena);
}

static void test_route_params_no_match(void) {
    Arena *arena = createArena(1024);
    RouteParams params;
    
    // Different number of segments
    bool matches = parseRouteParams("/users/:id", "/users", &params, arena);
    TEST_ASSERT_FALSE(matches);
    
    // Different path segments
    matches = parseRouteParams("/users/:id", "/posts/123", &params, arena);
    TEST_ASSERT_FALSE(matches);
    
    // Extra segments
    matches = parseRouteParams("/users/:id", "/users/123/extra", &params, arena);
    TEST_ASSERT_FALSE(matches);
    
    freeArena(arena);
}

int run_route_params_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_route_params_basic);
    RUN_TEST(test_route_params_multiple);
    RUN_TEST(test_route_params_no_match);
    return UNITY_END();
}
