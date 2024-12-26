#include "../test/unity/unity.h"
#include "../src/parser.h"
#include "../src/ast.h"
#include "test_runners.h"

// Function prototype
int run_parser_tests(void);

static void test_parser_init(void) {
    Parser parser;
    initParser(&parser, "test input");
    
    TEST_ASSERT_NOT_NULL(parser.arena);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, parser.current.type);
    TEST_ASSERT_EQUAL(TOKEN_UNKNOWN, parser.previous.type);
    
    freeArena(parser.arena);
}

static void test_parse_minimal_website(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  name \"Test Site\"\n"
        "  author \"Test Author\"\n"
        "  version \"1.0\"\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_EQUAL_STRING("\"Test Site\"", website->name);
    TEST_ASSERT_EQUAL_STRING("\"Test Author\"", website->author);
    TEST_ASSERT_EQUAL_STRING("\"1.0\"", website->version);
    TEST_ASSERT_NULL(website->pageHead);
    TEST_ASSERT_NULL(website->styleHead);
    TEST_ASSERT_NULL(website->layoutHead);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_page(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  pages {\n"
        "    page \"home\" {\n"
        "      route \"/\"\n"
        "      layout \"main\"\n"
        "      content {\n"
        "        \"content\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->pageHead);
    
    PageNode *page = website->pageHead;
    TEST_ASSERT_EQUAL_STRING("\"home\"", page->identifier);
    TEST_ASSERT_EQUAL_STRING("\"/\"", page->route);
    TEST_ASSERT_EQUAL_STRING("\"main\"", page->layout);
    TEST_ASSERT_NOT_NULL(page->contentHead);
    TEST_ASSERT_EQUAL_STRING("content", page->contentHead->type);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_styles(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  styles {\n"
        "    \"body\" {\n"
        "      \"background-color\" \"#fff\"\n"
        "      \"color\" \"#000\"\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->styleHead);
    
    StyleBlockNode *style = website->styleHead;
    TEST_ASSERT_EQUAL_STRING("\"body\"", style->selector);
    TEST_ASSERT_NOT_NULL(style->propHead);
    TEST_ASSERT_EQUAL_STRING("\"background-color\"", style->propHead->property);
    TEST_ASSERT_EQUAL_STRING("\"#fff\"", style->propHead->value);
    TEST_ASSERT_EQUAL_STRING("\"color\"", style->propHead->next->property);
    TEST_ASSERT_EQUAL_STRING("\"#000\"", style->propHead->next->value);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_layout(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  layouts {\n"
        "    \"main\" {\n"
        "      content {\n"
        "        \"content\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    
    LayoutNode *layout = website->layoutHead;
    TEST_ASSERT_EQUAL_STRING("\"main\"", layout->identifier);
    TEST_ASSERT_NOT_NULL(layout->bodyContent);
    TEST_ASSERT_EQUAL_STRING("content", layout->bodyContent->type);
    
    freeArena(parser.arena);
}

static void test_parse_error_handling(void) {
    Parser parser;
    const char *input = "website { invalid }";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_port(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  port 3000\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(3000, website->port);
    
    freeArena(parser.arena);
}

static void test_parse_nested_content(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  pages {\n"
        "    page \"test\" {\n"
        "      content {\n"
        "        div {\n"
        "          p \"nested\"\n"
        "          span \"content\"\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->contentHead);
    TEST_ASSERT_EQUAL_STRING("div", website->pageHead->contentHead->type);
    
    freeArena(parser.arena);
}

static void test_parse_error_recovery(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  pages {\n"
        "    page \"test\" {\n"
        "      invalid_token\n"
        "      route \"/test\"\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->pageHead);
    
    freeArena(parser.arena);
}

static void test_parse_complex_website(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  name \"Test Site\"\n"
        "  author \"Test Author\"\n"
        "  version \"1.0\"\n"
        "  port 8080\n"
        "  layouts {\n"
        "    \"main\" {\n"
        "      content {\n"
        "        h1 \"Site Header\"\n"
        "        p \"Welcome to our website\"\n"
        "        \"content\"\n"
        "        p \"Footer text\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  styles {\n"
        "    \"body\" {\n"
        "      \"margin\" \"0\"\n"
        "      \"padding\" \"20px\"\n"
        "    }\n"
        "    \"div\" {\n"
        "      \"color\" \"#333\"\n"
        "    }\n"
        "  }\n"
        "  pages {\n"
        "    page \"index\" {\n"
        "      route \"/\"\n"
        "      layout \"main\"\n"
        "      content {\n"
        "        h1 \"Welcome!\"\n"
        "        p {\n"
        "          link \"/about\" \"Learn more about our site\"\n"
        "        }\n"
        "        p \"This is a regular paragraph.\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_EQUAL(8080, website->port);
    TEST_ASSERT_EQUAL_STRING("\"Test Site\"", website->name);
    TEST_ASSERT_EQUAL_STRING("\"Test Author\"", website->author);
    TEST_ASSERT_EQUAL_STRING("\"1.0\"", website->version);
    
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    
    TEST_ASSERT_NOT_NULL(website->styleHead);
    TEST_ASSERT_NOT_NULL(website->pageHead);
    
    freeArena(parser.arena);
}

static void test_parse_invalid_constructs(void) {
    Parser parser;
    
    // Test invalid page without route
    const char *input1 = 
        "website {\n"
        "  pages {\n"
        "    page \"test\" {\n"
        "      route \"/test\"\n"
        "      content {}\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input1);
    WebsiteNode *website = parseProgram(&parser);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    freeArena(parser.arena);
    
    // Test invalid layout without content
    const char *input2 = 
        "website {\n"
        "  layouts {\n"
        "    \"main\" {\n"
        "      content {}\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input2);
    website = parseProgram(&parser);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    freeArena(parser.arena);
}

static void test_parse_website_with_api(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  api {\n"
        "    route \"/api/v1/users\"\n"
        "    method \"GET\"\n"
        "    response \"users\"\n"
        "  }\n"
        "  api {\n"
        "    route \"/api/v1/posts\"\n"
        "    method \"POST\"\n"
        "    response \"post\"\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->apiHead);
    
    ApiEndpoint *first = website->apiHead;
    TEST_ASSERT_EQUAL_STRING("\"/api/v1/users\"", first->route);
    TEST_ASSERT_EQUAL_STRING("\"GET\"", first->method);
    TEST_ASSERT_EQUAL_STRING("\"users\"", first->response);
    
    ApiEndpoint *second = first->next;
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_STRING("\"/api/v1/posts\"", second->route);
    TEST_ASSERT_EQUAL_STRING("\"POST\"", second->method);
    TEST_ASSERT_EQUAL_STRING("\"post\"", second->response);
    
    freeArena(parser.arena);
}

static void test_parse_invalid_api(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  api {\n"
        "    route \"/api/v1/users\"\n"
        "    invalid_token\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    
    freeArena(parser.arena);
}

int run_parser_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parser_init);
    RUN_TEST(test_parse_minimal_website);
    RUN_TEST(test_parse_website_with_page);
    RUN_TEST(test_parse_website_with_styles);
    RUN_TEST(test_parse_website_with_layout);
    RUN_TEST(test_parse_error_handling);
    RUN_TEST(test_parse_website_with_port);
    RUN_TEST(test_parse_nested_content);
    RUN_TEST(test_parse_error_recovery);
    RUN_TEST(test_parse_complex_website);
    RUN_TEST(test_parse_invalid_constructs);
    RUN_TEST(test_parse_website_with_api);
    RUN_TEST(test_parse_invalid_api);
    return UNITY_END();
}
