#include "../test/unity/unity.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/arena.h"
#include "test_runners.h"
#include <string.h>

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
    
    const char *input = "website pages page styles route layout name author version alt layouts port api method executeQuery query sql css mustache";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_WEBSITE,
        TOKEN_PAGES,
        TOKEN_PAGE,
        TOKEN_STYLES,
        TOKEN_ROUTE,
        TOKEN_LAYOUT,
        TOKEN_NAME,
        TOKEN_AUTHOR,
        TOKEN_VERSION,
        TOKEN_ALT,
        TOKEN_LAYOUTS,
        TOKEN_PORT,
        TOKEN_API,
        TOKEN_METHOD,
        TOKEN_EXECUTE_QUERY,
        TOKEN_QUERY,
        TOKEN_SQL,
        TOKEN_CSS,
        TOKEN_MUSTACHE
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
    TEST_ASSERT_EQUAL_STRING("hello world", token1.lexeme);
    
    Token token2 = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token2.type);
    TEST_ASSERT_EQUAL_STRING("test string", token2.lexeme);
    
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

    // Test unterminated triple-quoted string
    const char *unterminated_triple = "html \"\"\"\nsome content";
    initLexer(&lexer, unterminated_triple, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_HTML, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, token.type);
    TEST_ASSERT_EQUAL_STRING("Unterminated triple-quoted string.", token.lexeme);

    // Test invalid range syntax
    const char *invalid_range = "1..";
    initLexer(&lexer, invalid_range, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, token.type);
    TEST_ASSERT_EQUAL_STRING("Expected number after '..' in range.", token.lexeme);

    // Test invalid decimal number
    const char *invalid_decimal = "port 123.";
    initLexer(&lexer, invalid_decimal, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_PORT, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, token.type);
    TEST_ASSERT_EQUAL_STRING("Expected digit after decimal point.", token.lexeme);
    
    freeArena(parser.arena);
}

static void test_lexer_edge_cases(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "\"test\\n\\t\\\"\"";
    initLexer(&lexer, input, &parser);
    
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("test\\n\\t\\\"", token.lexeme);
    
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
    TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    TEST_ASSERT_EQUAL_STRING("123", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_NUMBER, token.type);
    TEST_ASSERT_EQUAL_STRING("123.456", token.lexeme);
    
    // Test invalid number format
    initLexer(&lexer, "123.456.789", &parser);
    token = getNextToken(&lexer);
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
        "    executeQuery \"users\"\n"
        "}";
    
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_API,
        TOKEN_OPEN_BRACE,
        TOKEN_ROUTE,
        TOKEN_STRING,
        TOKEN_METHOD,
        TOKEN_STRING,
        TOKEN_EXECUTE_QUERY,
        TOKEN_STRING,
        TOKEN_CLOSE_BRACE
    };
    
    const char *expectedStrings[] = {
        NULL,           // TOKEN_API
        NULL,           // TOKEN_OPEN_BRACE
        NULL,           // TOKEN_ROUTE
        "/api/v1/users",
        NULL,           // TOKEN_METHOD
        "GET",
        NULL,           // TOKEN_EXECUTE_QUERY
        "users",
        NULL            // TOKEN_CLOSE_BRACE
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
        
        if (expectedStrings[i] != NULL) {
            TEST_ASSERT_EQUAL_STRING(expectedStrings[i], token.lexeme);
        }
    }
    
    Token eof = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, eof.type);
    
    freeArena(parser.arena);
}

static void test_lexer_raw_blocks(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test HTML raw block
    const char *html_input = 
        "html {\n"
        "    <div class=\"test\">\n"
        "        <h1>Hello World</h1>\n"
        "    </div>\n"
        "}";
    
    initLexer(&lexer, html_input, &parser);
    
    // Should get HTML token first
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_HTML, token.type);
    
    // Then get raw block containing the HTML content
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "<div"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "Hello World"));
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test SQL raw block
    const char *sql_input = 
        "sql {\n"
        "    SELECT * FROM users\n"
        "    WHERE id = 1\n"
        "}";
    
    initLexer(&lexer, sql_input, &parser);
    
    // Should get SQL token first
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_SQL, token.type);
    
    // Then get raw block containing the SQL content
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "SELECT"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "FROM users"));
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test CSS raw block (old behavior for now)
    const char *css_input = 
        "css {\n"
        "    body { color: blue; }\n"
        "}";
    
    initLexer(&lexer, css_input, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "body"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "color: blue;"));
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_api_response_fields(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "executeQuery \"users\" [name, age, email]";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_EXECUTE_QUERY,
        TOKEN_STRING,
        TOKEN_OPEN_BRACKET,
        TOKEN_STRING,
        TOKEN_COMMA,
        TOKEN_STRING,
        TOKEN_COMMA,
        TOKEN_STRING,
        TOKEN_CLOSE_BRACKET
    };
    
    const char *expectedStrings[] = {
        NULL,       // TOKEN_EXECUTE_QUERY
        "users",    // TOKEN_STRING
        NULL,       // TOKEN_OPEN_BRACKET
        "name",     // TOKEN_STRING
        NULL,       // TOKEN_COMMA
        "age",      // TOKEN_STRING
        NULL,       // TOKEN_COMMA
        "email",    // TOKEN_STRING
        NULL        // TOKEN_CLOSE_BRACKET
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
        
        if (expectedStrings[i] != NULL) {
            TEST_ASSERT_EQUAL_STRING(expectedStrings[i], token.lexeme);
        }
    }
    
    freeArena(parser.arena);
}

static void test_lexer_nested_css_blocks(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = 
        "css {\n"
        "    body {\n"
        "        background: #ffffff;\n"
        "        color: #333;\n"
        "    }\n"
        "    h1 {\n"
        "        color: #ff6600;\n"
        "    }\n"
        "}";
    
    initLexer(&lexer, input, &parser);
    
    // Should get raw block containing all the CSS content
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    
    // Verify the raw block contains all the nested CSS
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "body {"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "background: #ffffff;"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "color: #333;"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "h1 {"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "color: #ff6600;"));
    
    // Should get EOF
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_jq_block(void) {
    Parser parser;
    parser.arena = createArena(1024);
    
    const char *input = 
        "jq {\n"
        "    .rows | map({\n"
        "        name: .name,\n"
        "        email: .email\n"
        "    })\n"
        "}";
    
    Lexer lexer;
    initLexer(&lexer, input, &parser);
    
    // Should get JQ token first
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_JQ, token.type);
    
    // Then get raw block containing the JQ content
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    
    // Verify the raw block contains the JQ filter
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, ".rows | map({"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "name: .name"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "email: .email"));
    
    // Should get EOF
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_lua_block(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "lua { function doSomething() end }";
    initLexer(&lexer, input, &parser);
    
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_LUA, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_filter_keywords(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "jq lua";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_JQ,
        TOKEN_LUA
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
    }
    
    Token eof = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, eof.type);
    
    freeArena(parser.arena);
}

static void test_lexer_transform_and_script(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = 
        "transform {\n"
        "    name \"formatEmployee\"\n"
        "    jq {\n"
        "        .rows | map({ id, name })\n"
        "    }\n"
        "}\n"
        "script {\n"
        "    name \"validateInput\"\n"
        "    lua {\n"
        "        if not input then return false end\n"
        "        return true\n"
        "    }\n"
        "}\n"
        "executeTransform \"formatEmployee\"\n"
        "executeScript \"validateInput\"";
    
    initLexer(&lexer, input, &parser);
    
    // Test transform definition
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_TRANSFORM, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_OPEN_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_NAME, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("formatEmployee", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_JQ, token.type);
    
    // Test script definition
    while (token.type != TOKEN_SCRIPT) {
        token = getNextToken(&lexer);
    }
    TEST_ASSERT_EQUAL(TOKEN_SCRIPT, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_OPEN_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_NAME, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("validateInput", token.lexeme);
    
    // Test execute commands
    while (token.type != TOKEN_EXECUTE_TRANSFORM) {
        token = getNextToken(&lexer);
    }
    TEST_ASSERT_EQUAL(TOKEN_EXECUTE_TRANSFORM, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("formatEmployee", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EXECUTE_SCRIPT, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("validateInput", token.lexeme);
    
    freeArena(parser.arena);
}

static void test_lexer_comments(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test single line comments
    const char *input = "website // This is a comment\nname";
    initLexer(&lexer, input, &parser);
    
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_WEBSITE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_NAME, token.type);
    
    // Test comment at end of file
    input = "website // Final comment";
    initLexer(&lexer, input, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_WEBSITE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test multiple comments
    input = "website // Comment 1\nname // Comment 2\nauthor";
    initLexer(&lexer, input, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_WEBSITE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_NAME, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_AUTHOR, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_mustache_block(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = 
        "mustache {\n"
        "    <div>{{name}}</div>\n"
        "    <p>{{description}}</p>\n"
        "}";
    
    initLexer(&lexer, input, &parser);
    
    // Should get MUSTACHE token first
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_MUSTACHE, token.type);
    
    // Then get raw block containing the mustache content
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    
    // Verify the raw block contains the mustache template
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "<div>{{name}}</div>"));
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "<p>{{description}}</p>"));
    
    // Should get EOF
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_include(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test basic include statement
    const char *input = "include \"somefile.webdsl\"";
    initLexer(&lexer, input, &parser);
    
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_INCLUDE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("somefile.webdsl", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    // Test include in website block
    const char *input2 = 
        "website {\n"
        "    include \"header.webdsl\"\n"
        "    include \"footer.webdsl\"\n"
        "}";
    
    initLexer(&lexer, input2, &parser);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_WEBSITE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_OPEN_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_INCLUDE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("header.webdsl", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_INCLUDE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_STRING, token.type);
    TEST_ASSERT_EQUAL_STRING("footer.webdsl", token.lexeme);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_CLOSE_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_error_success_blocks(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    // Test error and success blocks
    const char *input = 
        "error {\n"
        "    mustache {\n"
        "        <div>Error template</div>\n"
        "    }\n"
        "}\n"
        "success {\n"
        "    mustache {\n"
        "        <div>Success template</div>\n"
        "    }\n"
        "}";
    
    initLexer(&lexer, input, &parser);
    
    // Test error block
    Token token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_ERROR, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_OPEN_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_MUSTACHE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "Error template"));
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_CLOSE_BRACE, token.type);
    
    // Test success block
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_SUCCESS, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_OPEN_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_MUSTACHE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_RAW_BLOCK, token.type);
    TEST_ASSERT_NOT_NULL(strstr(token.lexeme, "Success template"));
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_CLOSE_BRACE, token.type);
    
    token = getNextToken(&lexer);
    TEST_ASSERT_EQUAL(TOKEN_EOF, token.type);
    
    freeArena(parser.arena);
}

static void test_lexer_env_vars(void) {
    Lexer lexer;
    Parser parser = {0};
    parser.arena = createArena(1024);
    
    const char *input = "port $PORT database $DATABASE_URL version $VERSION";
    initLexer(&lexer, input, &parser);
    
    TokenType expected[] = {
        TOKEN_PORT,
        TOKEN_ENV_VAR,
        TOKEN_DATABASE,
        TOKEN_ENV_VAR,
        TOKEN_VERSION,
        TOKEN_ENV_VAR
    };
    
    const char *expectedStrings[] = {
        NULL,           // TOKEN_PORT
        "$PORT",
        NULL,           // TOKEN_DATABASE
        "$DATABASE_URL",
        NULL,           // TOKEN_VERSION
        "$VERSION"
    };
    
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token token = getNextToken(&lexer);
        TEST_ASSERT_EQUAL(expected[i], token.type);
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
    RUN_TEST(test_lexer_raw_blocks);
    RUN_TEST(test_lexer_api_response_fields);
    RUN_TEST(test_lexer_nested_css_blocks);
    RUN_TEST(test_lexer_jq_block);
    RUN_TEST(test_lexer_lua_block);
    RUN_TEST(test_lexer_filter_keywords);
    RUN_TEST(test_lexer_transform_and_script);
    RUN_TEST(test_lexer_comments);
    RUN_TEST(test_lexer_mustache_block);
    RUN_TEST(test_lexer_include);
    RUN_TEST(test_lexer_error_success_blocks);
    RUN_TEST(test_lexer_env_vars);
    return UNITY_END();
}
