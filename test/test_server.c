#include "../test/unity/unity.h"
#include "../src/server/server.h"
#include "../src/server/handler.h"
#include "../src/server/routing.h"
#include "../src/server/db.h"
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
    website->port = makeNumber(3000);
    website->databaseUrl = makeString(arena, "postgresql://localhost/express-test?gssencmode=disable");
    website->baseUrl = arenaDupString(arena, "http://localhost:3000");
    website->author = arenaDupString(arena, "Test Author");
    website->version = arenaDupString(arena, "1.0.0");
    
    // Create a test API endpoint
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(api, 0, sizeof(ApiEndpoint));  // Initialize all fields to 0/NULL
    api->route = arenaDupString(arena, "/api/test");
    api->method = arenaDupString(arena, "GET");
    api->uses_pipeline = true;  // Set this to true since we're using a pipeline
    
    // Create SQL pipeline step
    PipelineStepNode *sqlStep =
    arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(sqlStep, 0, sizeof(PipelineStepNode));
    sqlStep->type = STEP_SQL;
    sqlStep->code = arenaDupString(arena, "SELECT 1");
    
    // Create Lua pipeline step
    PipelineStepNode *luaStep =
    arenaAlloc(arena, sizeof(PipelineStepNode));
    memset(luaStep, 0, sizeof(PipelineStepNode));
    luaStep->type = STEP_LUA;
    luaStep->code = arenaDupString(arena, "return { status = 'ok' }");
    
    // Link steps together
    sqlStep->next = luaStep;
    api->pipeline = sqlStep;
    
    // Add API to website
    website->apiHead = api;
    api->next = NULL;  // Explicitly set next pointer to NULL
    
    // Initialize other pointers to NULL
    website->pageHead = NULL;
    website->scriptHead = NULL;
    website->transformHead = NULL;
    website->queryHead = NULL;
    
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
    memset(page, 0, sizeof(PageNode)); // Ensure all fields are zeroed
    page->route = arenaDupString(arena, "/test");
    page->identifier = arenaDupString(arena, "test");
    page->next = NULL;
    website->pageHead = page;

    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);

    // Test route finding
    RouteParams params = {0};
    PageNode *found = findPage("/test", &params, arena);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("/test", found->route);
    TEST_ASSERT_EQUAL(0, params.count); // No parameters for exact match

    // Test API route finding
    RouteParams apiParams = {0};
    ApiEndpoint *api = findApi("/api/test", "GET", &apiParams, arena);
    TEST_ASSERT_NOT_NULL(api);
    TEST_ASSERT_EQUAL_STRING("/api/test", api->route);
    TEST_ASSERT_EQUAL_STRING("GET", api->method);

    stopServer();
    freeArena(arena);
}

static void test_server_routing_with_params(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);

    // Add a test page with route parameters
    PageNode *page = arenaAlloc(arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode)); // Zero all fields
    page->route = arenaDupString(arena, "/notes/:id/comments/:comment_id");
    page->identifier = arenaDupString(arena, "test");
    page->next = NULL;
    website->pageHead = page;

    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);

    // Test route finding with parameters
    RouteParams params = {0};
    PageNode *found = findPage("/notes/123/comments/456", &params, arena);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("/notes/:id/comments/:comment_id", found->route);

    // Verify extracted parameters
    TEST_ASSERT_EQUAL(2, params.count);
    TEST_ASSERT_EQUAL_STRING("id", params.params[0].name);
    TEST_ASSERT_EQUAL_STRING("123", params.params[0].value);
    TEST_ASSERT_EQUAL_STRING("comment_id", params.params[1].name);
    TEST_ASSERT_EQUAL_STRING("456", params.params[1].value);

    stopServer();
    freeArena(arena);
}

static void test_server_api_routing_with_params(void) {
    Arena *arena = createArena(1024 * 64);
    WebsiteNode *website = createTestWebsite(arena);

    // Add an API endpoint
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/users/:userId/posts/:postId/comments");
    api->method = arenaDupString(arena, "GET");
    api->uses_pipeline = true;
    api->pipeline = NULL;
    api->next = website->apiHead;
    website->apiHead = api;

    ServerContext *ctx = startServer(website, arena);
    TEST_ASSERT_NOT_NULL(ctx);

    // Test API endpoint finding
    RouteParams params = {0};
    ApiEndpoint *found = findApi("/api/users/123/posts/456/comments", "GET", &params, arena);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("/api/users/:userId/posts/:postId/comments", found->route);
    TEST_ASSERT_EQUAL_STRING("GET", found->method);
    
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
    RouteParams params = {0};
    ApiEndpoint *found = findApi("/api/validate", "POST", &params, arena);
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

static void test_route_matching(void) {
    Arena *arena = createArena(1024 * 1024);  // 1MB arena
    
    // Create a minimal website for testing
    WebsiteNode *website = createTestWebsite(arena);
    
    initRequestJsonArena(arena);
    
    // Add a test API endpoint
    ApiEndpoint *api = arenaAlloc(arena, sizeof(ApiEndpoint));
    memset(api, 0, sizeof(ApiEndpoint));
    api->route = arenaDupString(arena, "/api/test");
    api->method = arenaDupString(arena, "GET");
    website->apiHead = api;
    
    // Add a test page
    PageNode *page = arenaAlloc(arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode));
    page->route = arenaDupString(arena, "/");
    page->identifier = arenaDupString(arena, "home");
    website->pageHead = page;
    
    // Initialize server context (needed for route maps)
    startServer(website, arena);
    
    // Test API route
    RouteMatch apiMatch = findRoute("/api/test", "GET", arena);
    TEST_ASSERT_EQUAL(ROUTE_TYPE_API, apiMatch.type);
    TEST_ASSERT_NOT_NULL(apiMatch.endpoint.api);
    TEST_ASSERT_EQUAL_STRING("/api/test", apiMatch.endpoint.api->route);
    
    // Test page route
    RouteMatch pageMatch = findRoute("/", "GET", arena);
    TEST_ASSERT_EQUAL(ROUTE_TYPE_PAGE, pageMatch.type);
    TEST_ASSERT_NOT_NULL(pageMatch.endpoint.page);
    TEST_ASSERT_EQUAL_STRING("/", pageMatch.endpoint.page->route);
    
    // Test non-existent route
    RouteMatch noMatch = findRoute("/nonexistent", "GET", arena);
    TEST_ASSERT_EQUAL(ROUTE_TYPE_NONE, noMatch.type);
    TEST_ASSERT_NULL(noMatch.endpoint.api);
    
    stopServer();
    freeArena(arena);
}

int run_server_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_server_init);
    RUN_TEST(test_server_routing);
    RUN_TEST(test_server_routing_with_params);
    RUN_TEST(test_server_api_validation);
    RUN_TEST(test_server_database_connection);
    RUN_TEST(test_server_request_context);
    RUN_TEST(test_server_api_routing_with_params);  
    RUN_TEST(test_route_matching);
    return UNITY_END();
}
