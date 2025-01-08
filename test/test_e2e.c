#include "../test/unity/unity.h"
#include "../src/server/server.h"
#include "../src/parser.h"
#include "../src/file_utils.h"
#include "../src/website.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <jansson.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"

// Function prototype
int run_e2e_tests(void);

// Test configuration files
static const char *TEST_CONFIG = 
"website {\n"
"  name \"E2E Test Site\"\n"
"  port 3456\n"
"  database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
"  \n"
"  layouts {\n"
"    \"main\" {\n"
"      content {\n"
"        h1 \"Test Layout\"\n"
"        \"content\"\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  pages {\n"
"    page \"home\" {\n"
"      route \"/\"\n"
"      layout \"main\"\n"
"      content {\n"
"        h1 \"Welcome\"\n"
"        p \"Test content\"\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  api {\n"
"    route \"/api/test/pipeline\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      sql {\n"
"        SELECT 1 as num, 'test' as str\n"
"      }\n"
"      lua {\n"
"        request.transformed = true\n"
"        return request\n"
"      }\n"
"      jq {\n"
"        { result: { string: .rows[0].str, transformed: .transformed } }\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  api {\n"
"    route \"/api/test/validation\"\n"
"    method \"POST\"\n"
"    fields {\n"
"      \"email\" {\n"
"        type \"string\"\n"
"        required true\n"
"        format \"email\"\n"
"      }\n"
"    }\n"
"    pipeline {\n"
"      jq {\n"
"        {\n"
"          success: true,\n"
"          data: .body\n"
"        }\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  query {\n"
"    name \"testQuery\"\n"
"    params [\"id\"]\n"
"    sql {\n"
"      SELECT $1::int as input\n"
"    }\n"
"  }\n"
"  \n"
"  api {\n"
"    route \"/api/test/query\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      lua {\n"
"        return { params = { query.id } }\n"
"      }\n"
"      executeQuery \"testQuery\"\n"
"      jq {\n"
"        {\n"
"          result: (.rows[0].input | tonumber)\n"
"        }\n"
"      }\n"
"    }\n"
"  }\n"
"}\n";

static const char *TEST_FILE = "test_e2e_config.webdsl";

// CURL response buffer struct
typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

// CURL write callback
static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *mem = (ResponseBuffer *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;  // Out of memory
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Helper to make HTTP requests
static json_t* makeRequest(const char *url, const char *method, const char *data) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_slist_append(NULL, "Content-Type: application/json"));
    }
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }
    
    json_error_t error;
    // Use standard malloc/free for JSON parsing instead of arena
    json_set_alloc_funcs(malloc, free);
    json_t *json = json_loads(response.data, 0, &error);
    if (!json) {
        printf("JSON parse error: %s\n", error.text);
    }
    free(response.data);
    
    return json;
}

static void writeConfig(const char *config) {
    FILE *f = fopen(TEST_FILE, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "%s", config);
    fclose(f);
    sync();
}

static void test_full_website_lifecycle(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    sleep(1);
    
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("E2E Test Site", website->name);
    
    // Give server time to start
    sleep(1);
    
    // Test basic GET request to pipeline endpoint
    json_t *response = makeRequest("http://localhost:3456/api/test/pipeline", "GET", NULL);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify the pipeline transformations
    json_t *result = json_object_get(response, "result");
    TEST_ASSERT_NOT_NULL(result);
    
    // SQL returned 1
    // TEST_ASSERT_EQUAL(1, json_integer_value(json_object_get(result, "number")));
    TEST_ASSERT_EQUAL_STRING("test", json_string_value(json_object_get(result, "string")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(result, "transformed")));
    
    json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static void test_database_integration(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    sleep(1);
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    sleep(1);
    
    // Test prepared statement caching by making same query multiple times
    for (int i = 0; i < 5; i++) {
        char url[100];
        snprintf(url, sizeof(url), "http://localhost:3456/api/test/query?id=%d", i);
        
        json_t *response = makeRequest(url, "GET", NULL);
        TEST_ASSERT_NOT_NULL(response);
        
        TEST_ASSERT_EQUAL(i, json_number_value(json_object_get(response, "result")));
        json_decref(response);
    }
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static void test_pipeline_processing(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    sleep(1);
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    sleep(1);
    
    // Test pipeline with query parameters
    json_t *response = makeRequest(
        "http://localhost:3456/api/test/pipeline?param1=value1&param2=value2", 
        "GET", 
        NULL
    );
    TEST_ASSERT_NOT_NULL(response);
    
    json_t *result = json_object_get(response, "result");
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify expected fields
    TEST_ASSERT_EQUAL_STRING("test", json_string_value(json_object_get(result, "string")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(result, "transformed")));
    
    json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static void test_api_features(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    sleep(1);
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    sleep(1);
    
    // Test CORS headers
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Origin: http://example.com");
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:3456/api/test/pipeline");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // Get response headers
    ResponseBuffer header_response = {0};
    header_response.data = malloc(1);
    header_response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_response);
    
    CURLcode res = curl_easy_perform(curl);
    TEST_ASSERT_EQUAL(CURLE_OK, res);
    
    // Verify CORS headers
    TEST_ASSERT_NOT_NULL(strstr(header_response.data, "Access-Control-Allow-Origin"));
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    free(header_response.data);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

int run_e2e_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_website_lifecycle);
    RUN_TEST(test_database_integration);
    RUN_TEST(test_pipeline_processing);
    RUN_TEST(test_api_features);
    return UNITY_END();
}
#pragma clang diagnostic pop
