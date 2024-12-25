#include "../test/unity/unity.h"
#include "../src/lexer.h"
#include "../src/parser.h"

void setUp(void) {
    // This is run before each test
}

void tearDown(void) {
    // This is run after each test
}

static void test_lexer_init(void) {
    // Example test
    Lexer lexer;
    Parser parser = {0};
    
    initLexer(&lexer, "test input", &parser);
    
    TEST_ASSERT_NOT_NULL(lexer.start);
    TEST_ASSERT_NOT_NULL(lexer.current);
    TEST_ASSERT_EQUAL(1, lexer.line);
    TEST_ASSERT_EQUAL(&parser, lexer.parser);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lexer_init);
    return UNITY_END();
}
