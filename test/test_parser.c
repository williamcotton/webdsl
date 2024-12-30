#include "../test/unity/unity.h"
#include "../src/parser.h"
#include "../src/ast.h"
#include "test_runners.h"
#include <string.h>

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
    TEST_ASSERT_EQUAL_STRING("Test Site", website->name);
    TEST_ASSERT_EQUAL_STRING("Test Author", website->author);
    TEST_ASSERT_EQUAL_STRING("1.0", website->version);
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
    TEST_ASSERT_EQUAL_STRING("home", page->identifier);
    TEST_ASSERT_EQUAL_STRING("/", page->route);
    TEST_ASSERT_EQUAL_STRING("main", page->layout);
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
    TEST_ASSERT_EQUAL_STRING("body", style->selector);
    TEST_ASSERT_NOT_NULL(style->propHead);
    TEST_ASSERT_EQUAL_STRING("background-color", style->propHead->property);
    TEST_ASSERT_EQUAL_STRING("#fff", style->propHead->value);
    TEST_ASSERT_EQUAL_STRING("color", style->propHead->next->property);
    TEST_ASSERT_EQUAL_STRING("#000", style->propHead->next->value);
    
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
    TEST_ASSERT_EQUAL_STRING("main", layout->identifier);
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
    TEST_ASSERT_EQUAL_STRING("Test Site", website->name);
    TEST_ASSERT_EQUAL_STRING("Test Author", website->author);
    TEST_ASSERT_EQUAL_STRING("1.0", website->version);
    
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
        "    jsonResponse \"users\"\n"
        "  }\n"
        "  api {\n"
        "    route \"/api/v1/employees\"\n"
        "    method \"POST\"\n"
        "    jsonResponse \"insert_employee\" [name, age, position]\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->apiHead);
    
    ApiEndpoint *first = website->apiHead;
    TEST_ASSERT_EQUAL_STRING("/api/v1/users", first->route);
    TEST_ASSERT_EQUAL_STRING("GET", first->method);
    TEST_ASSERT_EQUAL_STRING("users", first->jsonResponse);
    TEST_ASSERT_NULL(first->fields);
    
    ApiEndpoint *second = first->next;
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_STRING("/api/v1/employees", second->route);
    TEST_ASSERT_EQUAL_STRING("POST", second->method);
    TEST_ASSERT_EQUAL_STRING("insert_employee", second->jsonResponse);
    
    // Test response fields
    TEST_ASSERT_NOT_NULL(second->fields);
    ResponseField *field = second->fields;
    TEST_ASSERT_EQUAL_STRING("name", field->name);
    field = field->next;
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("age", field->name);
    field = field->next;
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("position", field->name);
    TEST_ASSERT_NULL(field->next);
    
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

static void test_parse_website_with_query(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  query {\n"
        "    name \"users\"\n"
        "    sql {\n"
        "        SELECT * FROM users\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->queryHead);
    
    QueryNode *query = website->queryHead;
    TEST_ASSERT_EQUAL_STRING("users", query->name);
    TEST_ASSERT_NOT_NULL(query->sql);
    TEST_ASSERT_NOT_NULL(strstr(query->sql, "SELECT * FROM users"));
    
    freeArena(parser.arena);
}

static void test_parse_html_content(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  layouts {\n"
        "    \"main\" {\n"
        "      html \"\"\"\n"
        "      <header>Hello</header>\n"
        "      <!-- content -->\n"
        "      <footer>Footer</footer>\n"
        "      \"\"\"\n"
        "    }\n"
        "  }\n"
        "  pages {\n"
        "    page \"home\" {\n"
        "      route \"/\"\n"
        "      layout \"main\"\n"
        "      html \"\"\"\n"
        "      <h1>Welcome</h1>\n"
        "      <p>This is the home page.</p>\n"
        "      \"\"\"\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check layout HTML
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyContent);
    TEST_ASSERT_EQUAL_STRING("raw_html", website->layoutHead->bodyContent->type);
    TEST_ASSERT_TRUE(strstr(website->layoutHead->bodyContent->arg1, "<header>") != NULL);
    TEST_ASSERT_TRUE(strstr(website->layoutHead->bodyContent->arg1, "<!-- content -->") != NULL);
    
    // Check page HTML
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->contentHead);
    TEST_ASSERT_EQUAL_STRING("raw_html", website->pageHead->contentHead->type);
    TEST_ASSERT_TRUE(strstr(website->pageHead->contentHead->arg1, "<h1>Welcome</h1>") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_api_with_field_definitions(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  api {\n"
        "    route \"/api/v1/employees\"\n"
        "    method \"POST\"\n"
        "    fields {\n"
        "      \"name\" {\n"
        "        type \"string\"\n"
        "        required true\n"
        "        length 1..100\n"
        "      }\n"
        "      \"email\" {\n"
        "        type \"string\"\n"
        "        required true\n"
        "        format \"email\"\n"
        "      }\n"
        "    }\n"
        "    jsonResponse \"insertEmployee\" [name, email]\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->apiHead);
    
    ApiEndpoint *api = website->apiHead;
    TEST_ASSERT_NOT_NULL(api->apiFields);
    
    ApiField *nameField = api->apiFields;
    TEST_ASSERT_EQUAL_STRING("name", nameField->name);
    TEST_ASSERT_EQUAL_STRING("string", nameField->type);
    TEST_ASSERT_TRUE(nameField->required);
    TEST_ASSERT_EQUAL(1, nameField->minLength);
    TEST_ASSERT_EQUAL(100, nameField->maxLength);
    
    ApiField *emailField = nameField->next;
    TEST_ASSERT_EQUAL_STRING("email", emailField->name);
    TEST_ASSERT_EQUAL_STRING("string", emailField->type);
    TEST_ASSERT_TRUE(emailField->required);
    TEST_ASSERT_EQUAL_STRING("email", emailField->format);
    
    freeArena(parser.arena);
}

static void test_parse_raw_css_block(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  styles {\n"
        "    \"body\" {\n"
        "      css {\n"
        "        margin: 0;\n"
        "        padding: 20px;\n"
        "        font-family: Arial, sans-serif;\n"
        "      }\n"
        "    }\n"
        "    \".container\" {\n"
        "      \"width\" \"960px\"\n"
        "      \"margin\" \"0 auto\"\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->styleHead);
    
    // Check raw CSS block
    StyleBlockNode *bodyStyle = website->styleHead;
    TEST_ASSERT_EQUAL_STRING("body", bodyStyle->selector);
    TEST_ASSERT_NOT_NULL(bodyStyle->propHead);
    TEST_ASSERT_EQUAL_STRING("raw_css", bodyStyle->propHead->property);
    TEST_ASSERT_NOT_NULL(strstr(bodyStyle->propHead->value, "margin: 0;"));
    TEST_ASSERT_NOT_NULL(strstr(bodyStyle->propHead->value, "padding: 20px;"));
    TEST_ASSERT_NOT_NULL(strstr(bodyStyle->propHead->value, "font-family: Arial, sans-serif;"));
    
    // Check regular style block
    StyleBlockNode *containerStyle = bodyStyle->next;
    TEST_ASSERT_NOT_NULL(containerStyle);
    TEST_ASSERT_EQUAL_STRING(".container", containerStyle->selector);
    TEST_ASSERT_NOT_NULL(containerStyle->propHead);
    TEST_ASSERT_EQUAL_STRING("width", containerStyle->propHead->property);
    TEST_ASSERT_EQUAL_STRING("960px", containerStyle->propHead->value);
    TEST_ASSERT_EQUAL_STRING("margin", containerStyle->propHead->next->property);
    TEST_ASSERT_EQUAL_STRING("0 auto", containerStyle->propHead->next->value);
    
    freeArena(parser.arena);
}

static void test_parse_nested_css_block(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  styles {\n"
        "    css {\n"
        "      body {\n"
        "        background: #ffffff;\n"
        "        color: #333;\n"
        "      }\n"
        "      h1 {\n"
        "        color: #ff6600;\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->styleHead);
    
    // Check raw CSS block
    StyleBlockNode *style = website->styleHead;
    TEST_ASSERT_NOT_NULL(style);
    TEST_ASSERT_NOT_NULL(style->propHead);
    TEST_ASSERT_EQUAL_STRING("raw_css", style->propHead->property);
    
    // Check that the CSS content includes the nested blocks
    const char *cssValue = style->propHead->value;
    TEST_ASSERT_NOT_NULL(cssValue);
    TEST_ASSERT_NOT_NULL(strstr(cssValue, "body {"));
    TEST_ASSERT_NOT_NULL(strstr(cssValue, "background: #ffffff;"));
    TEST_ASSERT_NOT_NULL(strstr(cssValue, "color: #333;"));
    TEST_ASSERT_NOT_NULL(strstr(cssValue, "h1 {"));
    TEST_ASSERT_NOT_NULL(strstr(cssValue, "color: #ff6600;"));
    
    freeArena(parser.arena);
}

static void test_parse_api_with_jq_filter(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  api {\n"
        "    route \"/api/v1/users\"\n"
        "    method \"GET\"\n"
        "    jsonResponse \"users\"\n"
        "    jq {\n"
        "      .rows | map({\n"
        "        name: .name,\n"
        "        email: .email\n"
        "      })\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    // Debug output
    printf("\nTesting API with JQ filter:\n");
    printf("Parser had error: %d\n", parser.hadError);
    if (website && website->apiHead) {
        printf("API endpoint found:\n");
        printf("  Route: %s\n", website->apiHead->route);
        printf("  Method: %s\n", website->apiHead->method);
        printf("  Response: %s\n", website->apiHead->jsonResponse);
        printf("  JQ Filter: %s\n", website->apiHead->jqFilter ? website->apiHead->jqFilter : "NULL");
    } else {
        printf("No API endpoint found or website is NULL\n");
    }
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->apiHead);
    
    ApiEndpoint *api = website->apiHead;
    TEST_ASSERT_EQUAL_STRING("/api/v1/users", api->route);
    TEST_ASSERT_EQUAL_STRING("GET", api->method);
    TEST_ASSERT_EQUAL_STRING("users", api->jsonResponse);
    TEST_ASSERT_NOT_NULL(api->jqFilter);
    TEST_ASSERT_NOT_NULL(strstr(api->jqFilter, ".rows | map({"));
    TEST_ASSERT_NOT_NULL(strstr(api->jqFilter, "name: .name"));
    TEST_ASSERT_NOT_NULL(strstr(api->jqFilter, "email: .email"));
    
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
    RUN_TEST(test_parse_website_with_query);
    RUN_TEST(test_parse_html_content);
    RUN_TEST(test_parse_api_with_field_definitions);
    RUN_TEST(test_parse_raw_css_block);
    RUN_TEST(test_parse_nested_css_block);
    RUN_TEST(test_parse_api_with_jq_filter);
    return UNITY_END();
}
