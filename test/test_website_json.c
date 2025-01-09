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

static void test_website_to_json_with_layouts_and_queries(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create a layout with head and body content
    ContentNode *headContent = arenaAlloc(arena, sizeof(ContentNode));
    memset(headContent, 0, sizeof(ContentNode));
    headContent->type = arenaDupString(arena, "title");
    headContent->arg1 = arenaDupString(arena, "Test Title");

    ContentNode *bodyContent = arenaAlloc(arena, sizeof(ContentNode));
    memset(bodyContent, 0, sizeof(ContentNode));
    bodyContent->type = arenaDupString(arena, "div");
    bodyContent->arg1 = arenaDupString(arena, "content");
    bodyContent->arg2 = arenaDupString(arena, "main");

    LayoutNode *layout = arenaAlloc(arena, sizeof(LayoutNode));
    memset(layout, 0, sizeof(LayoutNode));
    layout->identifier = arenaDupString(arena, "main");
    layout->doctype = arenaDupString(arena, "html");
    layout->headContent = headContent;
    layout->bodyContent = bodyContent;

    // Create a query with parameters
    QueryParam *param1 = arenaAlloc(arena, sizeof(QueryParam));
    memset(param1, 0, sizeof(QueryParam));
    param1->name = arenaDupString(arena, "id");

    QueryParam *param2 = arenaAlloc(arena, sizeof(QueryParam));
    memset(param2, 0, sizeof(QueryParam));
    param2->name = arenaDupString(arena, "status");
    param2->next = NULL;
    param1->next = param2;

    QueryNode *query = arenaAlloc(arena, sizeof(QueryNode));
    memset(query, 0, sizeof(QueryNode));
    query->name = arenaDupString(arena, "get_user");
    query->sql = arenaDupString(arena, "SELECT * FROM users WHERE id = $1 AND status = $2");
    query->params = param1;

    // Create an API field with length constraints
    ApiField *field = arenaAlloc(arena, sizeof(ApiField));
    memset(field, 0, sizeof(ApiField));
    field->name = arenaDupString(arena, "username");
    field->type = arenaDupString(arena, "string");
    field->required = true;
    field->minLength = 3;
    field->maxLength = 20;

    ApiEndpoint *endpoint = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));
    endpoint->route = arenaDupString(arena, "/api/user");
    endpoint->method = arenaDupString(arena, "POST");
    endpoint->apiFields = field;

    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->layoutHead = layout;
    website->queryHead = query;
    website->apiHead = endpoint;

    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);

    // Test layouts
    json_t *layouts = json_object_get(json, "layouts");
    TEST_ASSERT_NOT_NULL(layouts);
    TEST_ASSERT_TRUE(json_is_object(layouts));

    json_t *main_layout = json_object_get(layouts, "main");
    TEST_ASSERT_NOT_NULL(main_layout);
    TEST_ASSERT_EQUAL_STRING("html", json_string_value(json_object_get(main_layout, "doctype")));

    json_t *head = json_object_get(main_layout, "head");
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_TRUE(json_is_array(head));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(head));

    json_t *head_content = json_array_get(head, 0);
    TEST_ASSERT_EQUAL_STRING("title", json_string_value(json_object_get(head_content, "type")));
    TEST_ASSERT_EQUAL_STRING("Test Title", json_string_value(json_object_get(head_content, "arg1")));

    json_t *body = json_object_get(main_layout, "body");
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_TRUE(json_is_array(body));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(body));

    json_t *body_content = json_array_get(body, 0);
    TEST_ASSERT_EQUAL_STRING("div", json_string_value(json_object_get(body_content, "type")));
    TEST_ASSERT_EQUAL_STRING("content", json_string_value(json_object_get(body_content, "arg1")));
    TEST_ASSERT_EQUAL_STRING("main", json_string_value(json_object_get(body_content, "arg2")));

    // Test queries
    json_t *queries = json_object_get(json, "queries");
    TEST_ASSERT_NOT_NULL(queries);
    TEST_ASSERT_TRUE(json_is_array(queries));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(queries));

    json_t *query_json = json_array_get(queries, 0);
    TEST_ASSERT_EQUAL_STRING("get_user", json_string_value(json_object_get(query_json, "name")));
    TEST_ASSERT_EQUAL_STRING("SELECT * FROM users WHERE id = $1 AND status = $2", json_string_value(json_object_get(query_json, "sql")));

    json_t *params = json_object_get(query_json, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(json_is_array(params));
    TEST_ASSERT_EQUAL_INT(2, json_array_size(params));
    TEST_ASSERT_EQUAL_STRING("id", json_string_value(json_array_get(params, 0)));
    TEST_ASSERT_EQUAL_STRING("status", json_string_value(json_array_get(params, 1)));

    // Test API fields with length constraints
    json_t *api = json_object_get(json, "api");
    TEST_ASSERT_NOT_NULL(api);
    TEST_ASSERT_TRUE(json_is_array(api));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(api));

    json_t *endpoint_json = json_array_get(api, 0);
    json_t *fields = json_object_get(endpoint_json, "fields");
    TEST_ASSERT_NOT_NULL(fields);

    json_t *username_field = json_object_get(fields, "username");
    TEST_ASSERT_NOT_NULL(username_field);
    TEST_ASSERT_EQUAL_STRING("string", json_string_value(json_object_get(username_field, "type")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(username_field, "required")));

    json_t *length = json_object_get(username_field, "length");
    TEST_ASSERT_NOT_NULL(length);
    TEST_ASSERT_EQUAL_INT(3, json_integer_value(json_object_get(length, "min")));
    TEST_ASSERT_EQUAL_INT(20, json_integer_value(json_object_get(length, "max")));

    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_dynamic_pipeline(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create a pipeline with a dynamic named step
    PipelineStepNode *step = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(step, 0, sizeof(PipelineStepNode));
    step->type = STEP_LUA;
    step->code = arenaDupString(arena, "return { dynamic = true }");
    step->name = arenaDupString(arena, "dynamic_step");
    step->is_dynamic = true;

    ApiEndpoint *endpoint = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));
    endpoint->route = arenaDupString(arena, "/api/dynamic");
    endpoint->method = arenaDupString(arena, "GET");
    endpoint->pipeline = step;
    endpoint->uses_pipeline = true;

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
    json_t *pipeline = json_object_get(endpoint_json, "pipeline");
    TEST_ASSERT_NOT_NULL(pipeline);
    TEST_ASSERT_TRUE(json_is_array(pipeline));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(pipeline));

    json_t *step_json = json_array_get(pipeline, 0);
    TEST_ASSERT_EQUAL_STRING("lua", json_string_value(json_object_get(step_json, "type")));
    TEST_ASSERT_EQUAL_STRING("return { dynamic = true }", json_string_value(json_object_get(step_json, "code")));
    TEST_ASSERT_EQUAL_STRING("dynamic_step", json_string_value(json_object_get(step_json, "name")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(step_json, "is_dynamic")));

    json_decref(json);
    freeArena(arena);
}

static void test_website_to_json_with_database(void) {
    Arena *arena = createArena(1024 * 64);
    
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Site");
    website->databaseUrl = arenaDupString(arena, "postgresql://localhost/test?sslmode=disable");

    json_t *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING("postgresql://localhost/test?sslmode=disable", 
                           json_string_value(json_object_get(json, "database")));

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
    RUN_TEST(test_website_to_json_with_layouts_and_queries);
    RUN_TEST(test_website_to_json_with_dynamic_pipeline);
    RUN_TEST(test_website_to_json_with_database);
    return UNITY_END();
}
