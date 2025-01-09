#ifndef TEST_RUNNERS_H
#define TEST_RUNNERS_H

// Core test runners
int run_lexer_tests(void);
int run_parser_tests(void);
int run_arena_tests(void);
int run_stringbuilder_tests(void);
int run_website_json_tests(void);

// Server test runners
int run_server_tests(void);
int run_server_html_tests(void);
int run_server_css_tests(void);
int run_server_validation_tests(void);

// Hotreload test runners
int run_website_tests(void);

// End-to-end test runners
int run_e2e_tests(void);

#endif // TEST_RUNNERS_H 
