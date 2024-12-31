#include "../../src/server/validation.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>

// Function prototype
int run_server_validation_tests(void);

static void test_validate_email_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "email",
        .type = "string",
        .format = "email",
        .required = true
    };

    // Test valid email
    char *result = validateField(arena, "test@example.com", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid email
    result = validateField(arena, "invalid-email", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid email format") != NULL);

    // Test required field with null value
    result = validateField(arena, NULL, &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "required") != NULL);

    freeArena(arena);
}

static void test_validate_number_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "age",
        .type = "number",
        .required = true,
        .validate.range = {
            .min = 18,
            .max = 100
        }
    };

    // Test valid number
    char *result = validateField(arena, "25", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid number format
    result = validateField(arena, "not-a-number", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Must be a valid number") != NULL);

    // Test number out of range
    result = validateField(arena, "150", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "between") != NULL);

    freeArena(arena);
}

static void test_validate_string_length(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "username",
        .type = "string",
        .required = true,
        .minLength = 3,
        .maxLength = 20
    };

    // Test valid string length
    char *result = validateField(arena, "testuser", &field);
    TEST_ASSERT_NULL(result);

    // Test too short
    result = validateField(arena, "ab", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Length must be between") != NULL);

    // Test too long - fix buffer overflow by making array large enough
    char longString[40] = "thisusernameistoolongtobevalid";
    result = validateField(arena, longString, &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Length must be between") != NULL);

    freeArena(arena);
}

int run_server_validation_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_validate_email_field);
    RUN_TEST(test_validate_number_field);
    RUN_TEST(test_validate_string_length);
    return UNITY_END();
}
