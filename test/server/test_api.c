#include "../../src/server/api.h"
#include "../../src/server/handler.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>

// Function prototype
int run_server_api_tests(void);

static void test_generate_api_error_response(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiEndpoint endpoint = {
        .route = "/api/test",
        .method = "POST",
        .fields = NULL,
        .jsonResponse = "test_query",
        .jqFilter = NULL
    };

    struct PostContext ctx = {0};
    ctx.post_data.value_count = 0;

    char *response = generateApiResponse(arena, &endpoint, &ctx);
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_TRUE(strstr(response, "error") != NULL);

    freeArena(arena);
}

int run_server_api_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_api_error_response);
    return UNITY_END();
}
