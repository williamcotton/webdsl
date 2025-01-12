#include "../test/unity/unity.h"
#include "../src/server/server.h"
#include "../src/server/handler.h"
#include "../src/server/api.h"
#include "../src/server/routing.h"
#include "../src/server/jq.h"
#include "../src/server/lua.h"
#include "../src/server/db.h"
#include "../src/parser.h"
#include "test_runners.h"
#include <string.h>
#include <microhttpd.h>

// Function prototype
int run_server_tests(void);

// Helper function to create a minimal website node for testing
static WebsiteNode* createTestWebsite(Arena *arena) {
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));  // Initialize all fields to 0/NULL
    
    website->name = arenaDupString(arena, "Test Site");
    website->port = 3000;
    website->databaseUrl = arenaDupString(arena, "postgresql://localhost/express-test?gssencmode=disable");
    website->base_url = arenaDupString(arena, "http://localhost:3000");
    website->author = arenaDupString(arena, "Test Author");
    website->version = arenaDupString(arena, "1.0.0");
    
    // Create a test API endpoint
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(api, 0, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/test");
    api->method = arenaDupString(arena, "GET");
    
    // Create SQL pipeline step
    PipelineStepNode *sqlStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(sqlStep, 0, sizeof(PipelineStepNode));
    sqlStep->type = STEP_SQL;
    sqlStep->code = arenaDupString(arena, "SELECT 1");
    
    // Create Lua pipeline step
    PipelineStepNode *luaStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(luaStep, 0, sizeof(PipelineStepNode));
    luaStep->type = STEP_LUA;
    luaStep->code = arenaDupString(arena, "return { status = 'ok' }");
    
    // Link steps together
    sqlStep->next = luaStep;
    api->pipeline = sqlStep;
    
    // Add API to website
    website->apiHead = api;
    
    return website;
}

static void test_server_init(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_NOT_NULL(ctx->website);
    TEST_ASSERT_NOT_NULL(ctx->arena);
    TEST_ASSERT_NOT_NULL(ctx->daemon);
    TEST_ASSERT_NOT_NULL(ctx->db);
    
    stopServer();
    freeArena(arena);
}

static void test_server_routing(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);
    
    // Add a test page
    PageNode *page = arenaAlloc(arena, sizeof(PageNode));
    page->route = arenaDupString(arena, "/test");
    page->identifier = arenaDupString(arena, "test");
    page->next = NULL;
    website->pageHead = page;
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    
    // Test route finding
    PageNode *found = findPage("/test");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("/test", found->route);
    
    // Test API route finding
    ApiEndpoint *api = findApi("/api/test", "GET");
    TEST_ASSERT_NOT_NULL(api);
    TEST_ASSERT_EQUAL_STRING("/api/test", api->route);
    TEST_ASSERT_EQUAL_STRING("GET", api->method);
    
    stopServer();
    freeArena(arena);
}

static void test_server_api_validation(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);
    
    // Add an API endpoint with field validation
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/validate");
    api->method = arenaDupString(arena, "POST");
    api->uses_pipeline = true;
    api->pipeline = NULL;  // Explicitly set pipeline to NULL since we're not using it for this test
    
    // Add a required email field
    ApiField *field = arenaAlloc(arena, sizeof(ApiField));
    field->name = arenaDupString(arena, "email");
    field->type = arenaDupString(arena, "string");
    field->format = arenaDupString(arena, "email");
    field->required = true;
    field->next = NULL;
    api->apiFields = field;
    
    api->next = website->apiHead;
    website->apiHead = api;
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    
    // Test API endpoint finding
    ApiEndpoint *found = findApi("/api/validate", "POST");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_NOT_NULL(found->apiFields);
    TEST_ASSERT_EQUAL_STRING("email", found->apiFields->name);
    TEST_ASSERT_TRUE(found->apiFields->required);
    
    stopServer();
    freeArena(arena);
}

static void test_server_database_connection(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_NOT_NULL(ctx->db);
    TEST_ASSERT_NOT_NULL(ctx->db->pool);
    
    // Test database connection pool
    TEST_ASSERT_TRUE(ctx->db->pool->size > 0);
    TEST_ASSERT_NOT_NULL(ctx->db->pool->connections);
    
    stopServer();
    freeArena(arena);
}

static void test_server_request_context(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    
    // Create a test request context
    struct RequestContext *reqctx = arenaAlloc(arena, sizeof(struct RequestContext));
    reqctx->arena = arena;
    reqctx->type = REQUEST_TYPE_GET;
    
    // Test request context initialization
    TEST_ASSERT_NOT_NULL(reqctx);
    TEST_ASSERT_NOT_NULL(reqctx->arena);
    TEST_ASSERT_EQUAL(REQUEST_TYPE_GET, reqctx->type);
    
    stopServer();
    freeArena(arena);
}

static void test_server_pipeline_execution(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);

    initRequestJsonArena(arena);

    // Create an API endpoint with a complex pipeline
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(api, 0, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/test/pipeline");
    api->method = arenaDupString(arena, "GET");
    api->uses_pipeline = true;
    
    // Create SQL pipeline step
    PipelineStepNode *sqlStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(sqlStep, 0, sizeof(PipelineStepNode));
    sqlStep->type = STEP_SQL;
    sqlStep->code = arenaDupString(arena, "SELECT 1 as num, 'test' as str");
    setupStepExecutor(sqlStep);
    
    // Create Lua pipeline step
    PipelineStepNode *luaStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(luaStep, 0, sizeof(PipelineStepNode));
    luaStep->type = STEP_LUA;
    luaStep->code = arenaDupString(arena, "request.transformed = true\nreturn request");
    setupStepExecutor(luaStep);
    
    // Create JQ pipeline step
    PipelineStepNode *jqStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(jqStep, 0, sizeof(PipelineStepNode));
    jqStep->type = STEP_JQ;
    jqStep->code = arenaDupString(arena, "{ result: { number: (.data[0].rows[0].num), string: .data[0].rows[0].str, transformed: .transformed } }");
    setupStepExecutor(jqStep);
    
    // Link steps together
    sqlStep->next = luaStep;
    luaStep->next = jqStep;
    api->pipeline = sqlStep;
    
    // Add API to website
    api->next = website->apiHead;
    website->apiHead = api;
    
    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);
    
    // Create a test request context
    json_t *requestContext = json_object();
    json_object_set_new(requestContext, "method", json_string("GET"));
    json_object_set_new(requestContext, "url", json_string("/api/test/pipeline"));
    json_object_set_new(requestContext, "version", json_string("HTTP/1.1"));
    json_object_set_new(requestContext, "query", json_object());
    json_object_set_new(requestContext, "headers", json_object());
    json_object_set_new(requestContext, "cookies", json_object());
    json_object_set_new(requestContext, "body", json_object());
    
    // Execute the pipeline
    json_t *response = generateApiResponse(arena, api, NULL, requestContext, ctx);
    TEST_ASSERT_NOT_NULL(response);

    char *response_str = json_dumps(response, 0);
    printf("response: %s\n", response_str);
    
    // Parse the response and verify the pipeline execution
    json_error_t error;
    json_t *result = json_loads(response_str, 0, &error);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify the pipeline transformations
    json_t *result_obj = json_object_get(result, "result");
    TEST_ASSERT_NOT_NULL(result_obj);

    json_t *number = json_object_get(result_obj, "number");
    TEST_ASSERT_NOT_NULL(number);
    TEST_ASSERT_TRUE(json_is_string(number));
    TEST_ASSERT_EQUAL_STRING("1", json_string_value(number));
    
    TEST_ASSERT_EQUAL_STRING("test", json_string_value(json_object_get(result_obj, "string")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(result_obj, "transformed")));
    
    json_decref(result);
    json_decref(requestContext);
    
    // Clean up thread-local JQ states
    void *table = pthread_getspecific(jq_key);
    if (table) {
        jq_thread_cleanup(table);
    }
    
    stopServer();
    cleanupRequestJsonArena();
    freeArena(arena);
}

static void test_server_post_request(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);

    initRequestJsonArena(arena);

    // Create an API endpoint that echoes back POST data
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(api, 0, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/test/echo");
    api->method = arenaDupString(arena, "POST");
    api->uses_pipeline = true;

    // Create JQ pipeline step that echoes back the request body
    PipelineStepNode *jqStep = arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(jqStep, 0, sizeof(PipelineStepNode));
    jqStep->type = STEP_JQ;
    jqStep->code = arenaDupString(arena, "{ echo: .body }");
    setupStepExecutor(jqStep);

    api->pipeline = jqStep;
    api->next = website->apiHead;
    website->apiHead = api;

    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);

    // Create a test request context with POST body
    json_t *requestContext = json_object();
    json_object_set_new(requestContext, "method", json_string("POST"));
    json_object_set_new(requestContext, "url", json_string("/api/test/echo"));
    json_object_set_new(requestContext, "version", json_string("HTTP/1.1"));
    json_object_set_new(requestContext, "query", json_object());
    json_object_set_new(requestContext, "headers", json_object());
    json_object_set_new(requestContext, "cookies", json_object());

    // Create a test POST body
    json_t *body = json_object();
    json_object_set_new(body, "message", json_string("Hello, World!"));
    json_object_set_new(body, "number", json_integer(42));
    json_object_set_new(requestContext, "body", body);

    // Execute the pipeline
    json_t *response = generateApiResponse(arena, api, NULL, requestContext, ctx);
    TEST_ASSERT_NOT_NULL(response);

    // Parse the response and verify it echoes the body
    json_error_t error;
    char *response_str = json_dumps(response, 0);
    printf("response: %s\n", response_str);
    json_t *result = json_loads(response_str, 0, &error);
    TEST_ASSERT_NOT_NULL(result);

    json_t *echo = json_object_get(result, "echo");
    TEST_ASSERT_NOT_NULL(echo);

    // Verify the echoed data matches our input
    TEST_ASSERT_EQUAL_STRING("Hello, World!", json_string_value(json_object_get(echo, "message")));
    TEST_ASSERT_EQUAL_INT(42, json_number_value(json_object_get(echo, "number")));

    json_decref(result);
    json_decref(requestContext);

    // Clean up thread-local JQ states
    void *table = pthread_getspecific(jq_key);
    if (table) {
        jq_thread_cleanup(table);
    }

    stopServer();
    cleanupRequestJsonArena();
    freeArena(arena);
}

int run_server_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_server_init);
    RUN_TEST(test_server_routing);
    RUN_TEST(test_server_api_validation);
    RUN_TEST(test_server_database_connection);
    RUN_TEST(test_server_request_context);
    RUN_TEST(test_server_pipeline_execution);
    RUN_TEST(test_server_post_request);
    return UNITY_END();
}
