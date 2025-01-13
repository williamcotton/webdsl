#include "../test/unity/unity.h"
#include "../src/server/server.h"
#include "../src/parser.h"
#include "../src/website.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <jansson.h>

// Function prototypes
int run_e2e_tests(void);
static void test_includes_functionality(void);
static void test_posts_endpoint(void);

// Test configuration files
static const char *TEST_CONFIG = 
"website {\n"
"  name \"E2E Test Site\"\n"
"  port 3456\n"
"  database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
"  \n"
"  layout {\n"
"    name \"main\"\n"
"    content {\n"
"      h1 \"Test Layout\"\n"
"      \"content\"\n"
"    }\n"
"  }\n"
"  \n"
"  page {\n"
"    name \"home\"\n"
"    route \"/\"\n"
"    layout \"main\"\n"
"    content {\n"
"      h1 \"Welcome\"\n"
"      p \"Test content\"\n"
"    }\n"
"  }\n"
"  \n"
"  api {\n"
"    route \"/api/v1/posts\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      lua {\n"
"        local url = \"http://jsonplaceholder.typicode.com/posts\"\n"
"        local response = fetch(url)\n"
"        return {\n"
"          data = response\n"
"        }\n"
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
"        { result: { string: .data[0].rows[0].str, transformed: .transformed } }\n"
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
"        { result: (.data[0].rows[0].input | tonumber) }\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  api {\n"
"    route \"/api/test/sql\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      lua {\n"
"        local result = sqlQuery(\"SELECT $1::int as num, $2::text as str\", {42, \"hello\"})\n"
"        return {\n"
"          data = result\n"
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
// Helper to make HTTP requests
static json_t* makeRequest(const char *url, const char *method, const char *data, char **headers_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    ResponseBuffer header_response = {0};
    header_response.data = malloc(1);
    header_response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // If headers are requested, capture them
    if (headers_out) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_response);
    }
    
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_slist_append(NULL, "Content-Type: application/json"));
    }
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        free(header_response.data);
        return NULL;
    }
    
    if (headers_out) {
        *headers_out = header_response.data;
    } else {
        free(header_response.data);
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
#pragma clang diagnostic pop

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
    json_t *response = makeRequest("http://localhost:3456/api/test/pipeline", "GET", NULL, NULL);
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
        
        json_t *response = makeRequest(url, "GET", NULL, NULL);
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
        NULL,
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
    char *headers = NULL;
    struct curl_slist *request_headers = NULL;
    request_headers = curl_slist_append(request_headers, "Origin: http://example.com");
    
    json_t *response = makeRequest("http://localhost:3456/api/test/pipeline", "GET", NULL, &headers);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify CORS headers
    TEST_ASSERT_NOT_NULL(strstr(headers, "Access-Control-Allow-Origin"));
    
    curl_slist_free_all(request_headers);
    json_decref(response);
    free(headers);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static const char *TEST_INCLUDE_CONFIG = 
"query {\n"
"    name \"getTest\"\n"
"    sql {\n"
"        SELECT 1 as id, 'test' as name\n"
"    }\n"
"}\n"
"\n"
"transform {\n"
"    name \"formatTest\"\n"
"    jq {\n"
"        { data: [{ id: .id, name: .name }], result: \"success\" }\n"
"    }\n"
"}\n"
"\n"
"api {\n"
"    route \"/api/v1/test\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"        executeQuery \"getTest\"\n"
"        executeTransform \"formatTest\"\n"
"    }\n"
"}\n";

void test_includes_functionality(void) {
    // Create temporary directory for test files
    char temp_dir[] = "/tmp/webdsl_test_XXXXXX";
    char *dir_path = mkdtemp(temp_dir);
    TEST_ASSERT_NOT_NULL(dir_path);
    
    // Create paths for config files
    char teams_path[256], main_path[256];
    snprintf(teams_path, sizeof(teams_path), "%s/test.webdsl", dir_path);
    snprintf(main_path, sizeof(main_path), "%s/main.webdsl", dir_path);
    
    // Write teams config
    FILE *teams = fopen(teams_path, "w");
    TEST_ASSERT_NOT_NULL(teams);
    fprintf(teams, "%s", TEST_INCLUDE_CONFIG);
    fclose(teams);
    
    // Create main config with relative include path
    char main_config[1024];
    snprintf(main_config, sizeof(main_config),
        "website {\n"
        "    name \"Include Test Site\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "    include \"%s/test.webdsl\"\n"
        "    page {\n"
        "        name \"home\"\n"
        "        route \"/\"\n"
        "        content { html \"Welcome\" }\n"
        "    }\n"
        "}\n",
        dir_path);
    
    // Write main config
    FILE *main = fopen(main_path, "w");
    TEST_ASSERT_NOT_NULL(main);
    fprintf(main, "%s", main_config);
    fclose(main);
    sync();
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, main_path);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("Include Test Site", website->name);
    
    // Give server time to start
    sleep(1);
    
    // Test included API endpoint
    char *headers = NULL;
    json_t *response = makeRequest("http://localhost:3456/api/v1/test?id=1", "GET", NULL, &headers);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(headers, "application/json"));
    
    // Verify response structure
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_NOT_NULL(json_object_get(response, "data"));
    TEST_ASSERT_EQUAL_STRING("success", json_string_value(json_object_get(response, "result")));
    
    free(headers);
    if (response) json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(teams_path);
    remove(main_path);
    rmdir(dir_path);
    sleep(1);
}

static void test_posts_endpoint(void) {
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    sleep(1);
    
    // Test GET request to posts endpoint
    json_t *response = makeRequest("http://localhost:3456/api/v1/posts", "GET", NULL, NULL);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify response structure
    TEST_ASSERT_NOT_NULL(json_object_get(response, "data"));
    TEST_ASSERT_TRUE(json_is_object(json_object_get(response, "data")));
    
    // Get the status and body from the response
    json_t *status = json_object_get(json_object_get(response, "data"), "status");
    json_t *body = json_object_get(json_object_get(response, "data"), "body");
    
    // Verify status code
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_EQUAL(200, json_integer_value(status));
    
    // Verify body is an array
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_TRUE(json_is_array(body));
    
    // Verify first post structure if array is not empty
    if (json_array_size(body) > 0) {
        json_t *first_post = json_array_get(body, 0);
        TEST_ASSERT_NOT_NULL(first_post);
        TEST_ASSERT_TRUE(json_is_object(first_post));
        
        // Verify required fields exist and have correct types
        TEST_ASSERT_NOT_NULL(json_object_get(first_post, "id"));
        TEST_ASSERT_TRUE(json_is_integer(json_object_get(first_post, "id")));
        
        TEST_ASSERT_NOT_NULL(json_object_get(first_post, "title"));
        TEST_ASSERT_TRUE(json_is_string(json_object_get(first_post, "title")));
        
        TEST_ASSERT_NOT_NULL(json_object_get(first_post, "body"));
        TEST_ASSERT_TRUE(json_is_string(json_object_get(first_post, "body")));
        
        TEST_ASSERT_NOT_NULL(json_object_get(first_post, "userId"));
        TEST_ASSERT_TRUE(json_is_integer(json_object_get(first_post, "userId")));
    }
    
    json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static void test_sql_query_endpoint(void) {
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    sleep(1);
    
    // Test GET request to SQL endpoint
    json_t *response = makeRequest("http://localhost:3456/api/test/sql", "GET", NULL, NULL);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify response structure
    json_t *data = json_object_get(response, "data");
    TEST_ASSERT_NOT_NULL(data);
    
    // Get the rows from the response
    json_t *result = json_object_get(data, "rows");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(json_is_array(result));
    TEST_ASSERT_TRUE(json_array_size(result) > 0);
    
    // Verify first row structure
    json_t *first_row = json_array_get(result, 0);
    TEST_ASSERT_NOT_NULL(first_row);
    TEST_ASSERT_TRUE(json_is_object(first_row));
    
    // Verify the values match what we queried
    TEST_ASSERT_EQUAL_STRING("42", json_string_value(json_object_get(first_row, "num")));
    TEST_ASSERT_EQUAL_STRING("hello", json_string_value(json_object_get(first_row, "str")));
    
    // Verify the query was included in response
    json_t *query = json_object_get(data, "query");
    TEST_ASSERT_NOT_NULL(query);
    TEST_ASSERT_TRUE(json_is_string(query));
    TEST_ASSERT_EQUAL_STRING("SELECT $1::int as num, $2::text as str", json_string_value(query));
    
    json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    sleep(1);
}

static void test_mustache_template_page(void) {
    // Write initial config with mustache template page
    const char *config = 
        "website {\n"
        "    name \"Mustache Test Site\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    layout {\n"
        "        name \"blog\"\n"
        "        html {\n"
        "            <header>\n"
        "                <h1>Blog Layout</h1>\n"
        "                <nav>\n"
        "                    <a href=\"/\">Home</a> |\n"
        "                    <a href=\"/blog\">Blog</a>\n"
        "                </nav>\n"
        "            </header>\n"
        "            <!-- content -->\n"
        "            <footer>\n"
        "                <p>Blog footer - Copyright 2024</p>\n"
        "            </footer>\n"
        "        }\n"
        "    }\n"
        "\n"
        "    page {\n"
        "        name \"mustache-test\"\n"
        "        route \"/mustache-test\"\n"
        "        layout \"blog\"\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    title: \"My Title\",\n"
        "                    message: \"My Message\",\n"
        "                    items: [\n"
        "                        { name: \"Wired up Item 1\" },\n"
        "                        { name: \"Wired up Item 2\" },\n"
        "                        { name: \"Wired up Item 3\" }\n"
        "                    ],\n"
        "                    url: .url,\n"
        "                    version: .version,\n"
        "                    method: .method\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        mustache {\n"
        "            <h1>{{title}}</h1>\n"
        "            <p>{{message}}</p>\n"
        "            <p>URL: {{url}}</p>\n"
        "            <p>Version: {{version}}</p>\n"
        "            <p>Method: {{method}}</p>\n"
        "            <ul>\n"
        "                {{#items}}\n"
        "                <li>{{name}}</li>\n"
        "                {{/items}}\n"
        "            </ul>\n"
        "        }\n"
        "    }\n"
        "}\n";
    
    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("Mustache Test Site", website->name);
    
    // Give server time to start
    sleep(1);
    
    // Make request to mustache template page
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl);
    
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:3456/mustache-test");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    CURLcode res = curl_easy_perform(curl);
    TEST_ASSERT_EQUAL(CURLE_OK, res);
    
    curl_easy_cleanup(curl);
    
    // Verify response contains expected content
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<h1>My Title</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<p>My Message</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<p>URL: /mustache-test</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<p>Method: GET</p>"));
    
    // Verify all items are present
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<li>Wired up Item 1</li>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<li>Wired up Item 2</li>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<li>Wired up Item 3</li>"));
    
    // Verify layout content is present
    TEST_ASSERT_NOT_NULL(strstr(response.data, "<h1>Blog Layout</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response.data, "Blog footer - Copyright 2024"));
    
    free(response.data);
    
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
    RUN_TEST(test_includes_functionality);
    RUN_TEST(test_posts_endpoint);
    RUN_TEST(test_sql_query_endpoint);
    RUN_TEST(test_mustache_template_page);
    return UNITY_END();
}
