#include "../../src/server/validation.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>
#include <regex.h>

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

static void test_validate_url_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "website",
        .type = "string",
        .format = "url",
        .required = true
    };

    // Test valid URLs
    char *result = validateField(arena, "https://example.com", &field);
    TEST_ASSERT_NULL(result);
    
    result = validateField(arena, "http://test.org/path", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid URLs
    result = validateField(arena, "not-a-url", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid URL format") != NULL);

    freeArena(arena);
}

static void test_validate_date_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "birthdate",
        .type = "string",
        .format = "date",
        .required = true
    };

    // Test valid dates
    char *result = validateField(arena, "2024-03-15", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid dates
    result = validateField(arena, "2024/03/15", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid date format") != NULL);

    result = validateField(arena, "2024-13-45", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid date format") != NULL);

    freeArena(arena);
}

static void test_validate_phone_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "phone",
        .type = "string",
        .format = "phone",
        .required = true
    };

    // Test valid phone numbers
    char *result = validateField(arena, "+1 (555) 123-4567", &field);
    TEST_ASSERT_NULL(result);
    
    result = validateField(arena, "555-123-4567", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid phone numbers
    result = validateField(arena, "abc-def-ghij", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid phone number") != NULL);

    freeArena(arena);
}

static void test_validate_pattern_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "code",
        .type = "string",
        .required = true,
        .validate.match.pattern = "^[A-Z]{3}[0-9]{3}$"
    };

    // Test valid pattern
    char *result = validateField(arena, "ABC123", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid patterns
    result = validateField(arena, "123ABC", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "must match pattern") != NULL);

    result = validateField(arena, "ABCD123", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "must match pattern") != NULL);

    freeArena(arena);
}

static void test_validate_uuid_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "id",
        .type = "string",
        .format = "uuid",
        .required = true
    };

    // Test valid UUID
    char *result = validateField(arena, "550e8400-e29b-41d4-a716-446655440000", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid UUIDs
    result = validateField(arena, "not-a-uuid", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid UUID format") != NULL);

    result = validateField(arena, "550e8400-e29b-41d4-a716", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid UUID format") != NULL);

    freeArena(arena);
}

static void test_validate_ipv4_field(void) {
    Arena *arena = createArena(1024 * 64);
    
    ApiField field = {
        .name = "ip",
        .type = "string",
        .format = "ipv4",
        .required = true
    };

    // Test valid IPv4
    char *result = validateField(arena, "192.168.1.1", &field);
    TEST_ASSERT_NULL(result);

    // Test invalid IPv4
    result = validateField(arena, "256.1.2.3", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid IPv4 address") != NULL);

    result = validateField(arena, "1.2.3", &field);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Invalid IPv4 address") != NULL);

    freeArena(arena);
}

int run_server_validation_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_validate_email_field);
    RUN_TEST(test_validate_number_field);
    RUN_TEST(test_validate_string_length);
    RUN_TEST(test_validate_url_field);
    RUN_TEST(test_validate_date_field);
    RUN_TEST(test_validate_phone_field);
    RUN_TEST(test_validate_pattern_field);
    RUN_TEST(test_validate_uuid_field);
    RUN_TEST(test_validate_ipv4_field);
    return UNITY_END();
}
