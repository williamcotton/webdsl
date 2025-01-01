#include "../../src/server/api.h"
#include "../../src/server/handler.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>

// Function prototype
int run_server_api_tests(void);

// Add mock request context for testing
static const char* getMockRequestContext(void) {
    return "{"
           "\"method\":\"GET\","
           "\"url\":\"/api/test\","
           "\"version\":\"HTTP/1.1\","
           "\"query\":{\"param\":\"value\"},"
           "\"headers\":{\"Content-Type\":\"application/json\"},"
           "\"cookies\":{},"
           "\"body\":{}"
           "}";
}

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

    char *response = generateApiResponse(arena, &endpoint, &ctx, getMockRequestContext());
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_TRUE(strstr(response, "error") != NULL);

    freeArena(arena);
}

static void test_api_validation_errors(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create test endpoint with validation rules
    ApiField *emailField = arenaAlloc(arena, sizeof(ApiField));
    memset(emailField, 0, sizeof(ApiField));
    emailField->name = arenaDupString(arena, "email");
    emailField->type = arenaDupString(arena, "string");
    emailField->format = arenaDupString(arena, "email");
    emailField->required = true;

    ApiField *ageField = arenaAlloc(arena, sizeof(ApiField));
    memset(ageField, 0, sizeof(ApiField));
    ageField->name = arenaDupString(arena, "age");
    ageField->type = arenaDupString(arena, "number");
    ageField->required = true;
    ageField->validate.range.min = 18;
    ageField->validate.range.max = 100;
    
    emailField->next = ageField;
    ageField->next = NULL;

    // Create response fields
    ResponseField *emailRespField = arenaAlloc(arena, sizeof(ResponseField));
    memset(emailRespField, 0, sizeof(ResponseField));
    emailRespField->name = arenaDupString(arena, "email");

    ResponseField *ageRespField = arenaAlloc(arena, sizeof(ResponseField));
    memset(ageRespField, 0, sizeof(ResponseField));
    ageRespField->name = arenaDupString(arena, "age");
    emailRespField->next = ageRespField;
    ageRespField->next = NULL;

    ApiEndpoint endpoint = {
        .route = "/api/register",
        .method = "POST",
        .apiFields = emailField,
        .fields = emailRespField,  // Now using ResponseField type
        .jsonResponse = "register_user"
    };

    // Test with invalid data
    struct PostContext ctx = {0};
    const char *temp_values[32] = {0};
    temp_values[0] = "invalid-email";
    temp_values[1] = "15";
    memcpy(ctx.post_data.values, temp_values, sizeof(char*) * 2);
    ctx.post_data.value_count = 2;

    char *response = generateApiResponse(arena, &endpoint, &ctx, getMockRequestContext());
    TEST_ASSERT_NOT_NULL(response);
    
    // Check for validation errors in the JSON response
    TEST_ASSERT_TRUE(strstr(response, "\"errors\"") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "\"email\"") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "\"age\"") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "Invalid email format") != NULL);
    TEST_ASSERT_TRUE(strstr(response, "Number must be between 18 and 100") != NULL);

    freeArena(arena);
}

static void test_api_get_request(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiEndpoint endpoint = {
        .route = "/api/users",
        .method = "GET",
        .jsonResponse = "get_users",
        .jqFilter = ".[] | {id, name}"  // Test JQ filter
    };

    char *response = generateApiResponse(arena, &endpoint, NULL, getMockRequestContext());
    TEST_ASSERT_NOT_NULL(response);
    // Note: Actual JSON validation would depend on database content
    
    freeArena(arena);
}

static void test_api_post_request_success(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create response fields - properly initialize all fields
    ResponseField *nameField = arenaAlloc(arena, sizeof(ResponseField));
    memset(nameField, 0, sizeof(ResponseField));  // Zero out the structure
    nameField->name = arenaDupString(arena, "name");  // Use arena to allocate string
    
    ResponseField *emailField = arenaAlloc(arena, sizeof(ResponseField));
    memset(emailField, 0, sizeof(ResponseField));  // Zero out the structure
    emailField->name = arenaDupString(arena, "email");
    nameField->next = emailField;
    emailField->next = NULL;  // Explicitly set next to NULL

    ApiEndpoint endpoint = {
        .route = "/api/users",
        .method = "POST",
        .fields = nameField,
        .jsonResponse = "create_user",
        .apiFields = NULL  // Explicitly set to NULL
    };

    // Test with valid data
    struct PostContext ctx = {0};
    const char *temp_values[32] = {0};  // Initialize array to zero
    temp_values[0] = "Test User";
    temp_values[1] = "test@example.com";
    memcpy(ctx.post_data.values, temp_values, sizeof(char*) * 2);
    ctx.post_data.value_count = 2;

    char *response = generateApiResponse(arena, &endpoint, &ctx, getMockRequestContext());
    TEST_ASSERT_NOT_NULL(response);
    
    freeArena(arena);
}

int run_server_api_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_api_error_response);
    RUN_TEST(test_api_validation_errors);
    RUN_TEST(test_api_get_request);
    RUN_TEST(test_api_post_request_success);
    return UNITY_END();
}
