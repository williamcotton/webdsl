#include "../src/website_json.h"
#include "../src/ast.h"
#include "../src/arena.h"
#include "unity/unity.h"
#include "test_runners.h"
#include <string.h>
#include <jansson.h>

// Function prototype
int run_website_json_tests(void);

static void test_website_to_json_minimal(void) {
    Arena *arena = createArena(1024 * 64);
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));  // Zero initialize all fields
    website->name = arenaDupString(arena, "Test Site");
    website->author = arenaDupString(arena, "Test Author");
    website->version = arenaDupString(arena, "1.0");
    website->port = 3000;
    
    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    TEST_ASSERT_EQUAL_STRING("Test Site", json_string_value(json_object_get(json, "name")));
    TEST_ASSERT_EQUAL_STRING("Test Author", json_string_value(json_object_get(json, "author")));
    TEST_ASSERT_EQUAL_STRING("1.0", json_string_value(json_object_get(json, "version")));
    TEST_ASSERT_EQUAL_INT(3000, json_integer_value(json_object_get(json, "port")));
    
    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_pages(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create a page with content
    ContentNode *content = arenaAlloc(arena, sizeof(ContentNode));
    memset(content, 0, sizeof(ContentNode));
    content->type = arenaDupString(arena, "h1");
    content->arg1 = arenaDupString(arena, "Welcome");
    
    PageNode *page = arenaAlloc(arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode));
    page->route = arenaDupString(arena, "/");
    page->layout = arenaDupString(arena, "main");
    page->contentHead = content;
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->pageHead = page;
    
    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    json_t *pages = json_object_get(json, "pages");
    TEST_ASSERT_NOT_NULL(pages);
    TEST_ASSERT_TRUE(json_is_array(pages));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(pages));
    
    json_t *page_json = json_array_get(pages, 0);
    TEST_ASSERT_EQUAL_STRING("/", json_string_value(json_object_get(page_json, "route")));
    TEST_ASSERT_EQUAL_STRING("main", json_string_value(json_object_get(page_json, "layout")));
    
    json_t *content_json = json_object_get(page_json, "content");
    TEST_ASSERT_NOT_NULL(content_json);
    TEST_ASSERT_TRUE(json_is_array(content_json));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(content_json));
    
    json_t *content_item = json_array_get(content_json, 0);
    TEST_ASSERT_EQUAL_STRING("h1", json_string_value(json_object_get(content_item, "type")));
    TEST_ASSERT_EQUAL_STRING("Welcome", json_string_value(json_object_get(content_item, "arg1")));
    
    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_styles(void) {
    Arena *arena = createArena(1024 * 64);
    
    StylePropNode *prop = arenaAlloc(arena, sizeof(StylePropNode));
    memset(prop, 0, sizeof(StylePropNode));
    prop->property = arenaDupString(arena, "color");
    prop->value = arenaDupString(arena, "#000");
    
    StyleBlockNode *block = arenaAlloc(arena, sizeof(StyleBlockNode));
    memset(block, 0, sizeof(StyleBlockNode));
    block->selector = arenaDupString(arena, "body");
    block->propHead = prop;
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->styleHead = block;
    
    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    json_t *styles = json_object_get(json, "styles");
    TEST_ASSERT_NOT_NULL(styles);
    TEST_ASSERT_TRUE(json_is_array(styles));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(styles));
    
    json_t *style_block = json_array_get(styles, 0);
    TEST_ASSERT_EQUAL_STRING("body", json_string_value(json_object_get(style_block, "selector")));
    
    json_t *properties = json_object_get(style_block, "properties");
    TEST_ASSERT_NOT_NULL(properties);
    TEST_ASSERT_EQUAL_STRING("#000", json_string_value(json_object_get(properties, "color")));
    
    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_api(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create an API endpoint with field validation
    ApiField *field = arenaAlloc(arena, sizeof(ApiField));
    memset(field, 0, sizeof(ApiField));
    field->name = arenaDupString(arena, "email");
    field->type = arenaDupString(arena, "string");
    field->format = arenaDupString(arena, "email");
    field->required = true;
    
    ApiEndpoint *endpoint = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));
    endpoint->route = arenaDupString(arena, "/api/test");
    endpoint->method = arenaDupString(arena, "POST");
    endpoint->apiFields = field;
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->apiHead = endpoint;
    
    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    json_t *api = json_object_get(json, "api");
    TEST_ASSERT_NOT_NULL(api);
    TEST_ASSERT_TRUE(json_is_array(api));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(api));
    
    json_t *endpoint_json = json_array_get(api, 0);
    TEST_ASSERT_EQUAL_STRING("/api/test", json_string_value(json_object_get(endpoint_json, "route")));
    TEST_ASSERT_EQUAL_STRING("POST", json_string_value(json_object_get(endpoint_json, "method")));
    
    json_t *fields = json_object_get(endpoint_json, "fields");
    TEST_ASSERT_NOT_NULL(fields);
    
    json_t *email_field = json_object_get(fields, "email");
    TEST_ASSERT_NOT_NULL(email_field);
    TEST_ASSERT_EQUAL_STRING("string", json_string_value(json_object_get(email_field, "type")));
    TEST_ASSERT_EQUAL_STRING("email", json_string_value(json_object_get(email_field, "format")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(email_field, "required")));
    
    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_pipeline(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create a pipeline with JQ and SQL steps
    PipelineStepNode *step1 = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(step1, 0, sizeof(PipelineStepNode));
    step1->type = STEP_JQ;
    step1->code = arenaDupString(arena, "{ query: .params }");
    
    PipelineStepNode *step2 = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(step2, 0, sizeof(PipelineStepNode));
    step2->type = STEP_SQL;
    step2->code = arenaDupString(arena, "SELECT * FROM users");
    
    step1->next = step2;
    
    ApiEndpoint *endpoint = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));
    endpoint->route = arenaDupString(arena, "/api/users");
    endpoint->method = arenaDupString(arena, "GET");
    endpoint->pipeline = step1;
    endpoint->uses_pipeline = true;
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->apiHead = endpoint;
    
    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    json_t *api = json_object_get(json, "api");
    TEST_ASSERT_NOT_NULL(api);
    
    json_t *endpoint_json = json_array_get(api, 0);
    json_t *pipeline = json_object_get(endpoint_json, "pipeline");
    TEST_ASSERT_NOT_NULL(pipeline);
    TEST_ASSERT_TRUE(json_is_array(pipeline));
    TEST_ASSERT_EQUAL_INT(2, json_array_size(pipeline));
    
    json_t *step1_json = json_array_get(pipeline, 0);
    TEST_ASSERT_EQUAL_STRING("jq", json_string_value(json_object_get(step1_json, "type")));
    TEST_ASSERT_EQUAL_STRING("{ query: .params }", json_string_value(json_object_get(step1_json, "code")));
    
    json_t *step2_json = json_array_get(pipeline, 1);
    TEST_ASSERT_EQUAL_STRING("sql", json_string_value(json_object_get(step2_json, "type")));
    TEST_ASSERT_EQUAL_STRING("SELECT * FROM users", json_string_value(json_object_get(step2_json, "code")));
    
    json_decref(json);
    freeArena(arena);
}

int run_website_json_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_website_to_json_minimal);
    RUN_TEST(test_website_to_json_with_pages);
    RUN_TEST(test_website_to_json_with_styles);
    RUN_TEST(test_website_to_json_with_api);
    RUN_TEST(test_website_to_json_with_pipeline);
    return UNITY_END();
}
