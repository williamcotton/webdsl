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

static void test_parse_website_with_page(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    content {\n"
        "      \"content\"\n"
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
        "  layout {\n"
        "    name \"main\"\n"
        "    content {\n"
        "      \"content\"\n"
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
        "  page {\n"
        "    name \"test\"\n"
        "    content {\n"
        "      div {\n"
        "        p \"nested\"\n"
        "        span \"content\"\n"
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

static void test_parse_complex_website(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  name \"Test Site\"\n"
        "  author \"Test Author\"\n"
        "  version \"1.0\"\n"
        "  port 8080\n"
        "  layout {\n"
        "    name \"main\"\n"
        "    content {\n"
        "      h1 \"Site Header\"\n"
        "      p \"Welcome to our website\"\n"
        "      \"content\"\n"
        "      p \"Footer text\"\n"
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
        "  page {\n"
        "    name \"index\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    content {\n"
        "      h1 \"Welcome!\"\n"
        "      p {\n"
        "        link \"/about\" \"Learn more about our site\"\n"
        "      }\n"
        "      p \"This is a regular paragraph.\"\n"
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
        "  page {\n"
        "    name \"test\"\n"
        "    route \"/test\"\n"
        "    content {}\n"
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
        "  layout {\n"
        "    name \"main\"\n"
        "    content {}\n"
        "  }\n"
        "}";
    
    initParser(&parser, input2);
    website = parseProgram(&parser);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
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
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyContent);
    TEST_ASSERT_EQUAL_STRING("raw_html", website->layoutHead->bodyContent->type);
    
    const char* layoutHtml = website->layoutHead->bodyContent->arg1;
    TEST_ASSERT_NOT_NULL(layoutHtml);
    TEST_ASSERT_TRUE(strstr(layoutHtml, "<header>") != NULL);
    TEST_ASSERT_TRUE(strstr(layoutHtml, "<!-- content -->") != NULL);
    
    // Check page HTML
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->contentHead);
    TEST_ASSERT_EQUAL_STRING("raw_html", website->pageHead->contentHead->type);
    
    const char* pageHtml = website->pageHead->contentHead->arg1;
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
    TEST_ASSERT_NOT_NULL(website->layoutHead->bodyContent);
    TEST_ASSERT_EQUAL_STRING("raw_mustache", website->layoutHead->bodyContent->type);
    
    const char* layoutTemplate = website->layoutHead->bodyContent->arg1;
    TEST_ASSERT_NOT_NULL(layoutTemplate);
    TEST_ASSERT_TRUE(strstr(layoutTemplate, "<div>{{name}}</div>") != NULL);
    TEST_ASSERT_TRUE(strstr(layoutTemplate, "<p>{{description}}</p>") != NULL);
    
    // Check page mustache template
    TEST_ASSERT_NOT_NULL(website->pageHead);
    TEST_ASSERT_NOT_NULL(website->pageHead->contentHead);
    TEST_ASSERT_EQUAL_STRING("raw_mustache", website->pageHead->contentHead->type);
    
    const char* pageTemplate = website->pageHead->contentHead->arg1;
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
    TEST_ASSERT_NOT_NULL(page->contentHead);
    TEST_ASSERT_EQUAL_STRING("raw_mustache", page->contentHead->type);
    TEST_ASSERT_NOT_NULL(page->contentHead->arg1);
    TEST_ASSERT_TRUE(strstr(page->contentHead->arg1, "{{title}}") != NULL);
    TEST_ASSERT_TRUE(strstr(page->contentHead->arg1, "{{count}}") != NULL);
    TEST_ASSERT_TRUE(strstr(page->contentHead->arg1, "{{#items}}") != NULL);
    
    freeArena(parser.arena);
}

static void test_parse_website_with_includes(void) {
    Parser parser;
    char temp_dir[] = "/tmp/webdsl_test_XXXXXX";
    char *dir_path = mkdtemp(temp_dir);
    TEST_ASSERT_NOT_NULL(dir_path);
    
    // Create paths for temporary files
    char header_path[256], footer_path[256], nav_path[256];
    snprintf(header_path, sizeof(header_path), "%s/test_header.webdsl", dir_path);
    snprintf(footer_path, sizeof(footer_path), "%s/test_footer.webdsl", dir_path);
    snprintf(nav_path, sizeof(nav_path), "%s/test_nav.webdsl", dir_path);
    
    // Create temporary files
    FILE *header = fopen(header_path, "w");
    TEST_ASSERT_NOT_NULL(header);
    fprintf(header, "page { name \"header\" route \"/header\" content { html \"header content\" } }");
    fclose(header);
    
    FILE *footer = fopen(footer_path, "w");
    TEST_ASSERT_NOT_NULL(footer);
    fprintf(footer, "page { name \"footer\" route \"/footer\" content { html \"footer content\" } }");
    fclose(footer);
    
    FILE *nav = fopen(nav_path, "w");
    TEST_ASSERT_NOT_NULL(nav);
    fprintf(nav, "page { name \"nav\" route \"/nav\" content { html \"nav content\" } }");
    fclose(nav);
    
    char input[1024];
    snprintf(input, sizeof(input),
        "website {\n"
        "    include \"%s\"\n"
        "    name \"Test Site\"\n"
        "    include \"%s\"\n"
        "    include \"%s\"\n"
        "}", header_path, footer_path, nav_path);
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    TEST_ASSERT_EQUAL_STRING("Test Site", website->name);
    
    // Check includes
    TEST_ASSERT_NOT_NULL(website->includeHead);
    
    IncludeNode *include = website->includeHead;
    TEST_ASSERT_EQUAL_STRING(header_path, include->filepath);
    
    include = include->next;
    TEST_ASSERT_NOT_NULL(include);
    TEST_ASSERT_EQUAL_STRING(footer_path, include->filepath);
    
    include = include->next;
    TEST_ASSERT_NOT_NULL(include);
    TEST_ASSERT_EQUAL_STRING(nav_path, include->filepath);
    
    TEST_ASSERT_NULL(include->next);
    
    // Clean up temporary files
    remove(header_path);
    remove(footer_path);
    remove(nav_path);
    rmdir(dir_path);
    
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

static void test_parse_page_with_error_success_blocks(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  page {\n"
        "    name \"notes-create\"\n"
        "    route \"/notes/create\"\n"
        "    method \"POST\"\n"
        "    fields {\n"
        "      \"title\" {\n"
        "        type \"string\"\n"
        "        required true\n"
        "      }\n"
        "    }\n"
        "    error {\n"
        "      mustache {\n"
        "        <div class=\"error-message\">{{error}}</div>\n"
        "      }\n"
        "    }\n"
        "    success {\n"
        "      mustache {\n"
        "        <div class=\"success-message\">Note created!</div>\n"
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
    TEST_ASSERT_EQUAL_STRING("notes-create", page->identifier);
    TEST_ASSERT_EQUAL_STRING("/notes/create", page->route);
    TEST_ASSERT_EQUAL_STRING("POST", page->method);
    
    // Check error block
    TEST_ASSERT_NOT_NULL(page->errorContent);
    TEST_ASSERT_EQUAL_STRING("raw_mustache", page->errorContent->type);
    TEST_ASSERT_NOT_NULL(page->errorContent->arg1);
    TEST_ASSERT_TRUE(strstr(page->errorContent->arg1, "<div class=\"error-message\">{{error}}</div>") != NULL);
    
    // Check success block
    TEST_ASSERT_NOT_NULL(page->successContent);
    TEST_ASSERT_EQUAL_STRING("raw_mustache", page->successContent->type);
    TEST_ASSERT_NOT_NULL(page->successContent->arg1);
    TEST_ASSERT_TRUE(strstr(page->successContent->arg1, "<div class=\"success-message\">Note created!</div>") != NULL);
    
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
    RUN_TEST(test_parse_website_with_includes);
    RUN_TEST(test_parse_include_errors);
    RUN_TEST(test_parse_page_with_error_success_blocks);
    return UNITY_END();
}
