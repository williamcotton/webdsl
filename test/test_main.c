#include "../test/unity/unity.h"
#include "test_runners.h"

void setUp(void) {
    // Global setup
}

void tearDown(void) {
    // Global teardown
}

int main(void) {
    int result = 0;
    
    result |= run_lexer_tests();
    result |= run_parser_tests();
    result |= run_arena_tests();
    result |= run_server_tests();
    result |= run_stringbuilder_tests();
    
    return result;
}
