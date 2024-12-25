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
    
    const char *input = "website pages page styles route layout content name author version alt layouts port";
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
        TOKEN_PORT
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

int run_lexer_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lexer_init);
    RUN_TEST(test_lexer_keywords);
    RUN_TEST(test_lexer_symbols);
    RUN_TEST(test_lexer_strings);
    RUN_TEST(test_lexer_numbers);
    RUN_TEST(test_lexer_line_counting);
    RUN_TEST(test_lexer_error_handling);
    return UNITY_END();
}
