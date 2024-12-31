#ifndef TEST_RUNNERS_H
#define TEST_RUNNERS_H

// Core test runners
int run_lexer_tests(void);
int run_parser_tests(void);
int run_arena_tests(void);
int run_stringbuilder_tests(void);

// Server test runners
int run_server_api_tests(void);
int run_server_html_tests(void);
int run_server_css_tests(void);
int run_server_validation_tests(void);

#endif // TEST_RUNNERS_H 
