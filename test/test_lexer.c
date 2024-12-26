#include "../test/unity/unity.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/arena.h"
#include "test_runners.h"

// Function prototype
int run_lexer_tests(void);

static void test_lexer_init(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    initLexer(&lexer, "test input", &parser);
    
    TEST_ASSERT_NOT_NULL(lexer.start);
    TEST_ASSERT_NOT_NULL(lexer.current);
    TEST_ASSERT_EQUAL(1, lexer.line);
    TEST_ASSERT_EQUAL(&parser, lexer.parser);
    
    freeArena(parser.arena);
}

static void test_lexer_keywords(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "website pages page styles route layout content name author version alt layouts port api method response";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_WEBSITE,
        TOKEN_PAGES,
        TOKEN_PAGE,
        TOKEN_STYLES,
        TOKEN_ROUTE,
        TOKEN_LAYOUT,
        TOKEN_CONTENT,
        TOKEN_NAME,
        TOKEN_AUTHOR,
        TOKEN_VERSION,
        TOKEN_ALT,
        TOKEN_LAYOUTS,
        TOKEN_PORT,
        TOKEN_API,
        TOKEN_METHOD,
        TOKEN_RESPONSE
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
    }
    
    Token eof = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, eof.type);
    
    freeArena(parser.arena);
}

static void test_lexer_symbols(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "{ } ( )";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_OPEN_BRACE,
        TOKEN_CLOSE_BRACE,
        TOKEN_OPEN_PAREN,
        TOKEN_CLOSE_PAREN
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
    }
    
    freeArena(parser.arena);
}

static void test_lexer_strings(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "\"hello world\" \"test string\"";
    initLexer(&lexer, input, &parser);
    
    Token token1 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token1.type);
    TEST_ASSERT_EQUAL_STRING("\"hello world\"", token1.lexeme);
    
    Token token2 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token2.type);
    TEST_ASSERT_EQUAL_STRING("\"test string\"", token2.lexeme);
    
    freeArena(parser.arena);
}

static void test_lexer_numbers(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "123 456 789";
    initLexer(&lexer, input, &parser);
    
    for (int i = 0; i < 3; i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    }
    
    freeArena(parser.arena);
}

static void test_lexer_line_counting(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "line1\nline2\n\nline4";
    initLexer(&lexer, input, &parser);
    
    Token token1 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(1, token1.line);
    
    Token token2 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(2, token2.line);
    
    Token token3 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(4, token3.line);
    
    freeArena(parser.arena);
}

static void test_lexer_error_handling(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "@#$";
    initLexer(&lexer, input, &parser);
    
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, token.type);
    TEST_ASSERT_EQUAL_STRING("Unexpected character.", token.lexeme);
    
    const char *unterminated = "\"unterminated string";
    initLexer(&lexer, unterminated, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, token.type);
    TEST_ASSERT_EQUAL_STRING("Unterminated string.", token.lexeme);
    
    freeArena(parser.arena);
}

static void test_lexer_edge_cases(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test empty input
    initLexer(&lexer, "", &parser);
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test whitespace only
    initLexer(&lexer, "   \t\n  \r\n", &parser);
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test escaped characters in strings
    initLexer(&lexer, "\"test\\n\\t\\\"\\\\\"", &parser);
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_number_formats(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test various number formats
    const char *input = "123 123.456 0.123 .123 123. 0";
    initLexer(&lexer, input, &parser);
    
    Token token;
    token = getNextToken(&lexer);
    printf("First token type: %u, lexeme: %s\n", token.type, token.lexeme);
    TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    TEST_ASSERT_EQUAL_STRING("123", token.lexeme);
    
    token = getNextToken(&lexer);
    printf("Second token type: %u, lexeme: %s\n", token.type, token.lexeme);
    TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    TEST_ASSERT_EQUAL_STRING("123.456", token.lexeme);
    
    // Test invalid number format
    initLexer(&lexer, "123.456.789", &parser);
    token = getNextToken(&lexer);
    printf("Invalid number token type: %u, lexeme: %s\n", token.type, token.lexeme);
    TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    TEST_ASSERT_EQUAL_STRING("123.456", token.lexeme);
    
    freeArena(parser.arena);
}

static void test_lexer_api_features(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test a complete API block
    const char *input = 
        "api {\n"
        "    route \"/api/v1/users\"\n"
        "    method \"GET\"\n"
        "    response \"users\"\n"
        "}";
    
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_API,
        TOKEN_OPEN_BRACE,
        TOKEN_ROUTE,
        TOKEN_STRING,
        TOKEN_METHOD,
        TOKEN_STRING,
        TOKEN_RESPONSE,
        TOKEN_STRING,
        TOKEN_CLOSE_BRACE
    };
    
    const char *expectedStrings[] = {
        NULL,           // TOKEN_API
        NULL,           // TOKEN_OPEN_BRACE
        NULL,           // TOKEN_ROUTE
        "\"/api/v1/users\"",
        NULL,           // TOKEN_METHOD
        "\"GET\"",
        NULL,           // TOKEN_RESPONSE
        "\"users\"",
        NULL            // TOKEN_CLOSE_BRACE
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
        
        // If we expect a string value, verify it
        if (expectedStrings[i] != NULL) {
            TEST_ASSERT_EQUAL_STRING(expectedStrings[i], token.lexeme);
        }
    }
    
    Token eof = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, eof.type);
    
    freeArena(parser.arena);
}

int run_lexer_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lexer_init);
    RUN_TEST(test_lexer_keywords);
    RUN_TEST(test_lexer_symbols);
    RUN_TEST(test_lexer_strings);
    RUN_TEST(test_lexer_numbers);
    RUN_TEST(test_lexer_line_counting);
    RUN_TEST(test_lexer_error_handling);
    RUN_TEST(test_lexer_edge_cases);
    RUN_TEST(test_lexer_number_formats);
    RUN_TEST(test_lexer_api_features);
    return UNITY_END();
}
