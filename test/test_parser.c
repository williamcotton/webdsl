#include "../test/unity/unity.h"
#include "../src/parser.h"
#include "../src/ast.h"
#include "test_runners.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

static void test_parse_error_recovery(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"test\"\n"
        "    invalid_token\n"
        "    route \"/test\"\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->pageHead);
    
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
        "  layout {\n"
        "    name \"main\"\n"
        "    html \"\"\"\n"
        "    <header>Hello</header>\n"
        "    <!-- content -->\n"
        "    <footer>Footer</footer>\n"
        "    \"\"\"\n"
        "  }\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    html \"\"\"\n"
        "    <h1>Welcome</h1>\n"
        "    <p>This is the home page.</p>\n"
        "    \"\"\"\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check layout HTML
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyTemplate);
    TEST_ASSERT_EQUAL(TEMPLATE_HTML, website->layoutHead->bodyTemplate->type);
    
    const char* layoutHtml = website->layoutHead->bodyTemplate->content;
    TEST_ASSERT_NOT_NULL(layoutHtml);
    TEST_ASSERT_TRUE(strstr(layoutHtml, "<header>") != NULL);
    TEST_ASSERT_TRUE(strstr(layoutHtml, "<!-- content -->") != NULL);
    
    // Check page HTML
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->template);
    TEST_ASSERT_EQUAL(TEMPLATE_HTML, website->pageHead->template->type);
    
    const char* pageHtml = website->pageHead->template->content;
    TEST_ASSERT_NOT_NULL(pageHtml);
    TEST_ASSERT_TRUE(strstr(pageHtml, "<h1>Welcome</h1>") != NULL);
    
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

static void test_parse_query_with_named_params(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  query {\n"
        "    name \"employees\"\n"
        "    params [department, role, limit]\n"
        "    sql {\n"
        "      SELECT * FROM employees\n"
        "      WHERE department = $1\n"
        "      AND role = $2\n"
        "      LIMIT $3\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_NOT_NULL(website->queryHead);
    
    QueryNode *query = website->queryHead;
    TEST_ASSERT_EQUAL_STRING("employees", query->name);
    TEST_ASSERT_NOT_NULL(query->params);
    
    // Verify parameter names
    QueryParam *param = query->params;
    TEST_ASSERT_EQUAL_STRING("department", param->name);
    param = param->next;
    TEST_ASSERT_EQUAL_STRING("role", param->name);
    param = param->next;
    TEST_ASSERT_EQUAL_STRING("limit", param->name);
    TEST_ASSERT_NULL(param->next);
    
    // Verify SQL contains parameter placeholders
    TEST_ASSERT_NOT_NULL(strstr(query->sql, "WHERE department = $1"));
    TEST_ASSERT_NOT_NULL(strstr(query->sql, "AND role = $2"));
    TEST_ASSERT_NOT_NULL(strstr(query->sql, "LIMIT $3"));
    
    freeArena(parser.arena);
}

static void test_parse_transform_and_script(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "    transform {\n"
        "        name \"formatEmployee\"\n"
        "        jq {\n"
        "            .rows | map({ id, name })\n"
        "        }\n"
        "    }\n"
        "    script {\n"
        "        name \"validateInput\"\n"
        "        lua {\n"
        "            if not input then return false end\n"
        "            return true\n"
        "        }\n"
        "    }\n"
        "    api {\n"
        "        route \"/api/employees\"\n"
        "        method \"GET\"\n"
        "        pipeline {\n"
        "            executeTransform \"formatEmployee\"\n"
        "            executeScript \"validateInput\"\n"
        "        }\n"
        "    }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check transform
    TEST_ASSERT_NOT_NULL(website->transformHead);
    TransformNode *transform = website->transformHead;
    TEST_ASSERT_EQUAL_STRING("formatEmployee", transform->name);
    TEST_ASSERT_EQUAL(FILTER_JQ, transform->type);
    TEST_ASSERT_NOT_NULL(transform->code);
    TEST_ASSERT_NOT_NULL(strstr(transform->code, ".rows | map"));
    
    // Check script
    TEST_ASSERT_NOT_NULL(website->scriptHead);
    ScriptNode *script = website->scriptHead;
    TEST_ASSERT_EQUAL_STRING("validateInput", script->name);
    TEST_ASSERT_EQUAL(FILTER_LUA, script->type);
    TEST_ASSERT_NOT_NULL(script->code);
    TEST_ASSERT_NOT_NULL(strstr(script->code, "if not input"));
    
    // Check pipeline steps
    TEST_ASSERT_NOT_NULL(website->apiHead);
    ApiEndpoint *api = website->apiHead;
    TEST_ASSERT_NOT_NULL(api->pipeline);
    
    PipelineStepNode *step = api->pipeline;
    TEST_ASSERT_EQUAL(STEP_JQ, step->type);
    TEST_ASSERT_EQUAL_STRING("formatEmployee", step->name);
    TEST_ASSERT_FALSE(step->is_dynamic);
    
    step = step->next;
    TEST_ASSERT_NOT_NULL(step);
    TEST_ASSERT_EQUAL(STEP_LUA, step->type);
    TEST_ASSERT_EQUAL_STRING("validateInput", step->name);
    TEST_ASSERT_FALSE(step->is_dynamic);
    
    freeArena(parser.arena);
}

static void test_parse_mustache_content(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  layout {\n"
        "    name \"main\"\n"
        "    mustache {\n"
        "      <div>{{name}}</div>\n"
        "      <p>{{description}}</p>\n"
        "    }\n"
        "  }\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    mustache {\n"
        "      <h1>{{title}}</h1>\n"
        "      <p>{{content}}</p>\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check layout mustache template
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyTemplate);
    TEST_ASSERT_EQUAL(TEMPLATE_MUSTACHE, website->layoutHead->bodyTemplate->type);
    
    const char* layoutTemplate = website->layoutHead->bodyTemplate->content;
    TEST_ASSERT_NOT_NULL(layoutTemplate);
    TEST_ASSERT_TRUE(strstr(layoutTemplate, "<div>{{name}}</div>") != NULL);
    TEST_ASSERT_TRUE(strstr(layoutTemplate, "<p>{{description}}</p>") != NULL);
    
    // Check page mustache template
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->template);
    TEST_ASSERT_EQUAL(TEMPLATE_MUSTACHE, website->pageHead->template->type);
    
    const char* pageTemplate = website->pageHead->template->content;
    TEST_ASSERT_NOT_NULL(pageTemplate);
    TEST_ASSERT_TRUE(strstr(pageTemplate, "<h1>{{title}}</h1>") != NULL);
    TEST_ASSERT_TRUE(strstr(pageTemplate, "<p>{{content}}</p>") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_page_with_pipeline(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    pipeline {\n"
        "      lua {\n"
        "        local data = {}\n"
        "        data.title = \"Welcome\"\n"
        "        data.items = {\"one\", \"two\", \"three\"}\n"
        "        return data\n"
        "      }\n"
        "      jq {\n"
        "        . + {count: (.items | length)}\n"
        "      }\n"
        "    }\n"
        "    mustache {\n"
        "      <h1>{{title}}</h1>\n"
        "      <p>Items ({{count}}):</p>\n"
        "      {{#items}}\n"
        "        <li>{{.}}</li>\n"
        "      {{/items}}\n"
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
    
    // Check pipeline
    TEST_ASSERT_NOT_NULL(page->pipeline);
    
    // Check first step (Lua)
    PipelineStepNode *step = page->pipeline;
    TEST_ASSERT_EQUAL(STEP_LUA, step->type);
    TEST_ASSERT_NOT_NULL(step->code);
    TEST_ASSERT_TRUE(strstr(step->code, "data.title = \"Welcome\"") != NULL);
    
    // Check second step (JQ)
    step = step->next;
    TEST_ASSERT_NOT_NULL(step);
    TEST_ASSERT_EQUAL(STEP_JQ, step->type);
    TEST_ASSERT_NOT_NULL(step->code);
    TEST_ASSERT_TRUE(strstr(step->code, ". + {count: (.items | length)}") != NULL);
    
    // Check mustache template
    TEST_ASSERT_NOT_NULL(page->template);
    TEST_ASSERT_EQUAL(TEMPLATE_MUSTACHE, page->template->type);
    TEST_ASSERT_NOT_NULL(page->template->content);
    TEST_ASSERT_TRUE(strstr(page->template->content, "{{title}}") != NULL);
    TEST_ASSERT_TRUE(strstr(page->template->content, "{{count}}") != NULL);
    TEST_ASSERT_TRUE(strstr(page->template->content, "{{#items}}") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_include_errors(void) {
    Parser parser;
    
    // Test missing filepath
    const char *input1 = 
        "website {\n"
        "    include\n"
        "}";
    
    initParser(&parser, input1);
    WebsiteNode *website = parseProgram(&parser);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    freeArena(parser.arena);
    
    // Test invalid filepath (not a string)
    const char *input2 = 
        "website {\n"
        "    include 123\n"
        "}";
    
    initParser(&parser, input2);
    website = parseProgram(&parser);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(1, parser.hadError);
    freeArena(parser.arena);
}

static void test_parse_page_with_error_success_templates(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"test\"\n"
        "    route \"/test\"\n"
        "    error {\n"
        "      mustache {\n"
        "        <div>Error: {{message}}</div>\n"
        "      }\n"
        "    }\n"
        "    success {\n"
        "      mustache {\n"
        "        <div>Success: {{message}}</div>\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check error template
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->errorTemplate);
    TEST_ASSERT_EQUAL(TEMPLATE_MUSTACHE, website->pageHead->errorTemplate->type);
    TEST_ASSERT_TRUE(strstr(website->pageHead->errorTemplate->content, "Error: {{message}}") != NULL);
    
    // Check success template
    TEST_ASSERT_NOT_NULL(website->pageHead->successTemplate);
    TEST_ASSERT_EQUAL(TEMPLATE_MUSTACHE, website->pageHead->successTemplate->type);
    TEST_ASSERT_TRUE(strstr(website->pageHead->successTemplate->content, "Success: {{message}}") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_content(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    html \"\"\"\n"
        "    <h1>Welcome</h1>\n"
        "    <p>This is the home page.</p>\n"
        "    \"\"\"\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check page content
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->template);
    TEST_ASSERT_EQUAL(TEMPLATE_HTML, website->pageHead->template->type);
    TEST_ASSERT_TRUE(strstr(website->pageHead->template->content, "<h1>Welcome</h1>") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_layout_with_content(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  layout {\n"
        "    name \"main\"\n"
        "    html \"\"\"\n"
        "    <header>Header</header>\n"
        "    <!-- content -->\n"
        "    <footer>Footer</footer>\n"
        "    \"\"\"\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    // Check layout content
    TEST_ASSERT_NOT_NULL(website->layoutHead);
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyTemplate);
    TEST_ASSERT_EQUAL(TEMPLATE_HTML, website->layoutHead->bodyTemplate->type);
    TEST_ASSERT_TRUE(strstr(website->layoutHead->bodyTemplate->content, "<header>Header</header>") != NULL);
    TEST_ASSERT_TRUE(strstr(website->layoutHead->bodyTemplate->content, "<!-- content -->") != NULL);
    
    freeArena(parser.arena);
}

int run_parser_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parser_init);
    RUN_TEST(test_parse_minimal_website);
    RUN_TEST(test_parse_website_with_styles);
    RUN_TEST(test_parse_error_handling);
    RUN_TEST(test_parse_website_with_port);
    RUN_TEST(test_parse_error_recovery);
    RUN_TEST(test_parse_invalid_api);
    RUN_TEST(test_parse_website_with_query);
    RUN_TEST(test_parse_html_content);
    RUN_TEST(test_parse_api_with_field_definitions);
    RUN_TEST(test_parse_raw_css_block);
    RUN_TEST(test_parse_nested_css_block);
    RUN_TEST(test_parse_query_with_named_params);
    RUN_TEST(test_parse_transform_and_script);
    RUN_TEST(test_parse_mustache_content);
    RUN_TEST(test_parse_page_with_pipeline);
    RUN_TEST(test_parse_include_errors);
    RUN_TEST(test_parse_page_with_error_success_templates);
    RUN_TEST(test_parse_website_with_content);
    RUN_TEST(test_parse_layout_with_content);
    return UNITY_END();
}
