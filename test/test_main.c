#include "unity/unity.h"
#include "test_runners.h"

void setUp(void) {
    // Global setup
}

void tearDown(void) {
    // Global teardown
}

int main(void) {
    int result = 0;
    
    // Run core tests
    result |= run_lexer_tests();
    result |= run_parser_tests();
    result |= run_arena_tests();
    result |= run_stringbuilder_tests();
    result |= run_website_json_tests();
    
    // Run server tests
    result |= run_server_tests();
    result |= run_server_css_tests();
    result |= run_server_validation_tests();
    
    // Run hotreload tests
    result |= run_website_tests();
    
    // Run end-to-end tests
    result |= run_e2e_tests();
    
    return result;
}
