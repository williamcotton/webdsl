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
"    html {\n"
"      <h1>Test Layout</h1>\n"
"      <!-- content -->\n"
"    }\n"
"  }\n"
"  \n"
"  page {\n"
"    name \"home\"\n"
"    route \"/\"\n"
"    layout \"main\"\n"
"    html {\n"
"      <h1>Welcome</h1>\n"
"      <p>Test content</p>\n"
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
"        return { sqlParams = { query.id } }\n"
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

// Helper to make POST requests and return the raw response
static char* makePostRequest(const char *url, const char *post_data, long *response_code_out, char **headers_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    ResponseBuffer header_response = {0};
    header_response.data = malloc(1);
    header_response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // Don't follow redirects
    
    // If headers are requested, capture them
    if (headers_out) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_response);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        free(header_response.data);
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    if (response_code_out) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code_out);
    }
    
    if (headers_out) {
        *headers_out = header_response.data;
    } else {
        free(header_response.data);
    }
    
    curl_easy_cleanup(curl);
    
    // For empty responses, return NULL
    if (response.size == 0) {
        free(response.data);
        return NULL;
    }
    
    return response.data;
}

// Helper to make JSON POST requests
static char* makeJsonPostRequest(const char *url, const char *json_data, long *response_code_out, char **headers_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    ResponseBuffer header_response = {0};
    header_response.data = malloc(1);
    header_response.size = 0;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // Don't follow redirects
    
    // If headers are requested, capture them
    if (headers_out) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_response);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        free(response.data);
        free(header_response.data);
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    if (response_code_out) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code_out);
    }
    
    if (headers_out) {
        *headers_out = header_response.data;
    } else {
        free(header_response.data);
    }
    
    curl_easy_cleanup(curl);
    
    // For empty responses, return NULL
    if (response.size == 0) {
        free(response.data);
        return NULL;
    }
    
    return response.data;
}

// Helper to make HTTP requests and return raw response
static char* makeRawRequest(const char *url, ResponseBuffer *response_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }
    
    if (response_out) {
        *response_out = response;
    }
    
    return response.data;
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
    
    
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("E2E Test Site", website->name);
    
    // Give server time to start
    
    
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
    
}

static void test_database_integration(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    
    
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
    
}

static void test_pipeline_processing(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    
    
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
    
}

static void test_api_features(void) {
    // Ensure clean state
    stopServer();
    remove(TEST_FILE);
    
    
    // Write config
    writeConfig(TEST_CONFIG);
    
    // Start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    
    
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
        "        html {\n"
        "            <h1>Welcome</h1>\n"
        "        }\n"
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
    
}

static void test_posts_endpoint(void) {
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    
    
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
    
}

static void test_sql_query_endpoint(void) {
    // Write initial config
    writeConfig(TEST_CONFIG);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    
    
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
    
    
    // Make request to mustache template page
    ResponseBuffer response = {0};
    char *response_data = makeRawRequest("http://localhost:3456/mustache-test", &response);
    TEST_ASSERT_NOT_NULL(response_data);
    
    // Verify response contains expected content
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>My Title</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>My Message</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>URL: /mustache-test</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Method: GET</p>"));
    
    // Verify all items are present
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<li>Wired up Item 1</li>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<li>Wired up Item 2</li>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<li>Wired up Item 3</li>"));
    
    // Verify layout content is present
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>Blog Layout</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "Blog footer - Copyright 2024"));
    
    free(response_data);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    
}

static void test_page_post_handler(void) {
    // Write initial config with POST handler page
    const char *config = 
        "website {\n"
        "    name \"Page POST Test\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    page {\n"
        "        route \"/test/form\"\n"
        "        method \"POST\"\n"
        "        layout \"main\"\n"
        "        fields {\n"
        "            \"message\" {\n"
        "                type \"string\"\n"
        "                required true\n"
        "            }\n"
        "        }\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    message: .body.message,\n"
        "                    method: .method,\n"
        "                    url: .url\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        mustache {\n"
        "            <div class=\"result\">\n"
        "                <h1>Form Processed</h1>\n"
        "                <p>Message: {{message}}</p>\n"
        "                <p>Method: {{method}}</p>\n"
        "                <p>URL: {{url}}</p>\n"
        "            </div>\n"
        "        }\n"
        "    }\n"
        "}\n";
    
    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    
    
    // Prepare POST data
    const char *post_data = "message=Hello%20World";
    
    // Make POST request
    long response_code;
    char *response_data = makePostRequest("http://localhost:3456/test/form", post_data, &response_code, NULL);
    TEST_ASSERT_NOT_NULL(response_data);
    TEST_ASSERT_EQUAL(200, response_code);
    
    // Verify response contains expected content
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>Form Processed</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Message: Hello World</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Method: POST</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>URL: /test/form</p>"));
    
    // Clean up
    free(response_data);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    
}

static void test_page_redirect(void) {
    // Write initial config with redirect page
    const char *config = 
        "website {\n"
        "    name \"Redirect Test Site\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    layout {\n"
        "        name \"main\"\n"
        "        html {\n"
        "            <h1>Test Layout</h1>\n"
        "            <!-- content -->\n"
        "        }\n"
        "    }\n"
        "\n"
        "    page {\n"
        "        route \"/test/redirect\"\n"
        "        layout \"main\"\n"
        "        method \"POST\"\n"
        "        fields {\n"
        "            \"message\" {\n"
        "                type \"string\"\n"
        "                required true\n"
        "            }\n"
        "        }\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    message: .body.message,\n"
        "                    method: .method,\n"
        "                    url: .url\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        redirect \"/test/destination\"\n"
        "    }\n"
        "}\n";
    
    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    

    // Prepare POST data
    const char *post_data = "message=Hello%20World";

    // Make request to redirect page
    long response_code;
    char *headers = NULL;
    char *response_data = makePostRequest("http://localhost:3456/test/redirect", post_data, &response_code, &headers);
    
    // Verify redirect response
    TEST_ASSERT_EQUAL(302, response_code);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "Location: /test/destination"));
    
    // For redirects, response_data should either be NULL or an empty response
    if (response_data) {
        TEST_ASSERT_EQUAL_STRING("", response_data);
    }
    
    // Clean up
    if (response_data) free(response_data);
    free(headers);
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    
}

static void test_route_params(void) {
    // Write initial config with parameterized routes
    const char *config = 
        "website {\n"
        "    name \"Route Params Test\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    api {\n"
        "        route \"/api/notes/:id/comments/:comment_id\"\n"
        "        method \"GET\"\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    params: .params,\n"
        "                    url: .url,\n"
        "                    method: .method\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "\n"
        "    api {\n"
        "        route \"/api/users/:userId/posts/:postId/comments\"\n"
        "        method \"GET\"\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    params: .params,\n"
        "                    url: .url\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n";

    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("Route Params Test", website->name);
    
    // Give server time to start
    
    
    // Test first route with parameters
    json_t *response = makeRequest("http://localhost:3456/api/notes/123/comments/456", "GET", NULL, NULL);

    TEST_ASSERT_NOT_NULL(response);
    
    // Verify response structure and parameters
    json_t *params = json_object_get(response, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(json_is_object(params));
    TEST_ASSERT_EQUAL_STRING("123", json_string_value(json_object_get(params, "id")));
    TEST_ASSERT_EQUAL_STRING("456", json_string_value(json_object_get(params, "comment_id")));
    TEST_ASSERT_EQUAL_STRING("/api/notes/123/comments/456", json_string_value(json_object_get(response, "url")));
    TEST_ASSERT_EQUAL_STRING("GET", json_string_value(json_object_get(response, "method")));
    
    json_decref(response);

    // Test second route with different parameters
    response = makeRequest("http://localhost:3456/api/users/789/posts/101/comments", "GET", NULL, NULL);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify second route parameters
    params = json_object_get(response, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(json_is_object(params));
    TEST_ASSERT_EQUAL_STRING("789", json_string_value(json_object_get(params, "userId")));
    TEST_ASSERT_EQUAL_STRING("101", json_string_value(json_object_get(params, "postId")));
    TEST_ASSERT_EQUAL_STRING("/api/users/789/posts/101/comments", json_string_value(json_object_get(response, "url")));
    
    json_decref(response);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    
}

static void test_json_post_endpoint(void) {
    // Write initial config with JSON POST endpoint
    const char *config = 
        "website {\n"
        "    name \"JSON POST Test\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    api {\n"
        "        route \"/api/test/json\"\n"
        "        method \"POST\"\n"
        "        fields {\n"
        "            \"name\" {\n"
        "                type \"string\"\n"
        "                required true\n"
        "                length 2..50\n"
        "            }\n"
        "            \"age\" {\n"
        "                type \"number\"\n"
        "                required true\n"
        "            }\n"
        "            \"email\" {\n"
        "                type \"string\"\n"
        "                format \"email\"\n"
        "                required true\n"
        "            }\n"
        "        }\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    success: true,\n"
        "                    data: {\n"
        "                        name: .body.name,\n"
        "                        age: (.body.age | tonumber),\n"
        "                        email: .body.email,\n"
        "                        processed: true\n"
        "                    },\n"
        "                    request: {\n"
        "                        method: .method,\n"
        "                        url: .url\n"
        "                    }\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n";

    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("JSON POST Test", website->name);
    
    // Give server time to start
    
    
    // Prepare JSON POST data
    const char *json_data = "{\n"
        "    \"name\": \"John Doe\",\n"
        "    \"age\": 30,\n"
        "    \"email\": \"john@example.com\"\n"
        "}";
    
    // Make JSON POST request
    long response_code;
    char *headers = NULL;
    char *response_data = makeJsonPostRequest(
        "http://localhost:3456/api/test/json",
        json_data,
        &response_code,
        &headers
    );
    
    // Verify response code and headers
    TEST_ASSERT_EQUAL(200, response_code);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "Content-Type: application/json"));
    
    // Use standard malloc/free for JSON parsing
    json_set_alloc_funcs(malloc, free);
    
    // Parse and verify response JSON
    json_error_t error;
    json_t *response = json_loads(response_data, 0, &error);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify response structure
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(response, "success")));
    
    json_t *data = json_object_get(response, "data");
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("John Doe", json_string_value(json_object_get(data, "name")));
    TEST_ASSERT_EQUAL(30, json_number_value(json_object_get(data, "age")));
    TEST_ASSERT_EQUAL_STRING("john@example.com", json_string_value(json_object_get(data, "email")));
    TEST_ASSERT_TRUE(json_boolean_value(json_object_get(data, "processed")));
    
    json_t *request = json_object_get(response, "request");
    TEST_ASSERT_NOT_NULL(request);
    TEST_ASSERT_EQUAL_STRING("POST", json_string_value(json_object_get(request, "method")));
    TEST_ASSERT_EQUAL_STRING("/api/test/json", json_string_value(json_object_get(request, "url")));
    
    // Clean up
    json_decref(response);
    free(response_data);
    free(headers);
    
    // Test validation failures
    const char *invalid_json_data = "{\n"
        "    \"name\": \"\",\n"
        "    \"age\": \"not a number\",\n"
        "    \"email\": \"not-an-email\"\n"
        "}";
    
    response_data = makeJsonPostRequest(
        "http://localhost:3456/api/test/json",
        invalid_json_data,
        &response_code,
        &headers
    );
    
    // Verify response code for validation error
    TEST_ASSERT_EQUAL(400, response_code);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "Content-Type: application/json"));
    
    // Use standard malloc/free for JSON parsing
    json_set_alloc_funcs(malloc, free);
    
    // Parse and verify error response JSON
    response = json_loads(response_data, 0, &error);
    TEST_ASSERT_NOT_NULL(response);
    
    // Verify error response structure
    TEST_ASSERT_FALSE(json_boolean_value(json_object_get(response, "success")));
    
    json_t *errors = json_object_get(response, "errors");
    TEST_ASSERT_NOT_NULL(errors);
    TEST_ASSERT_TRUE(json_is_object(errors));
    
    // Verify specific field validation errors
    TEST_ASSERT_NOT_NULL(json_object_get(errors, "name"));
    TEST_ASSERT_NOT_NULL(json_object_get(errors, "age")); 
    TEST_ASSERT_NOT_NULL(json_object_get(errors, "email"));
    
    // Clean up validation test
    json_decref(response);
    free(response_data);
    free(headers);
    
    // Stop server and clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
    
}

static void test_page_post_with_reference_data(void) {
    // Write initial config with POST handler page that has reference data
    const char *config = 
        "website {\n"
        "    name \"Page POST Reference Test\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    page {\n"
        "        route \"/test/form-with-ref\"\n"
        "        method \"POST\"\n"
        "        layout \"main\"\n"
        "        fields {\n"
        "            \"message\" {\n"
        "                type \"string\"\n"
        "                required true\n"
        "                length 5..50\n"
        "            }\n"
        "            \"category\" {\n"
        "                type \"string\"\n"
        "                required true\n"
        "            }\n"
        "        }\n"
        "        referenceData {\n"
        "            jq {\n"
        "                {\n"
        "                    categories: [\n"
        "                        { id: \"1\", name: \"Category 1\" },\n"
        "                        { id: \"2\", name: \"Category 2\" },\n"
        "                        { id: \"3\", name: \"Category 3\" }\n"
        "                    ]\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        pipeline {\n"
        "            jq {\n"
        "                {\n"
        "                    message: .body.message,\n"
        "                    category: .body.category,\n"
        "                    method: .method,\n"
        "                    url: .url\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        mustache {\n"
        "            <div class=\"result\">\n"
        "                <h1>Form Processed</h1>\n"
        "                <p>Message: {{message}}</p>\n"
        "                <p>Category: {{category}}</p>\n"
        "                <p>Method: {{method}}</p>\n"
        "                <p>URL: {{url}}</p>\n"
        "            </div>\n"
        "        }\n"
        "        error {\n"
        "            mustache {\n"
        "                <div class=\"error-form\">\n"
        "                    <h1>Form Error</h1>\n"
        "                    <form method=\"post\">\n"
        "                        <div>\n"
        "                            <label>Message:</label>\n"
        "                            <input type=\"text\" name=\"message\" value=\"{{values.message}}\">\n"
        "                            {{#errors.message}}<p class=\"error\">{{errors.message}}</p>{{/errors.message}}\n"
        "                        </div>\n"
        "                        <div>\n"
        "                            <label>Category:</label>\n"
        "                            <select name=\"category\">\n"
        "                                {{#categories}}\n"
        "                                <option value=\"{{id}}\" {{#selected}}selected{{/selected}}>{{name}}</option>\n"
        "                                {{/categories}}\n"
        "                            </select>\n"
        "                            {{#errors.category}}<p class=\"error\">{{errors.category}}</p>{{/errors.category}}\n"
        "                        </div>\n"
        "                        <button type=\"submit\">Submit</button>\n"
        "                    </form>\n"
        "                </div>\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n";
    
    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    
    
    // Test 1: Invalid submission (message too short)
    const char *invalid_post_data = "message=Hi&category=1";
    
    long response_code;
    char *response_data = makePostRequest("http://localhost:3456/test/form-with-ref", 
                                        invalid_post_data, &response_code, NULL);
    TEST_ASSERT_NOT_NULL(response_data);
    TEST_ASSERT_EQUAL(200, response_code);
    
    // Verify error response contains reference data and validation errors
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<div class=\"error-form\">"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "Category 1"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "Category 2"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "Category 3"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "value=\"Hi\""));  // Original value preserved
    TEST_ASSERT_NOT_NULL(strstr(response_data, "class=\"error\"")); // Error message shown
    
    free(response_data);
    
    // Test 2: Valid submission
    const char *valid_post_data = "message=Hello+World&category=2";
    
    response_data = makePostRequest("http://localhost:3456/test/form-with-ref", 
                                  valid_post_data, &response_code, NULL);
    TEST_ASSERT_NOT_NULL(response_data);
    TEST_ASSERT_EQUAL(200, response_code);
    
    // Verify success response
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>Form Processed</h1>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Message: Hello World</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Category: 2</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>Method: POST</p>"));
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<p>URL: /test/form-with-ref</p>"));
    
    // Clean up
    free(response_data);
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
}

static void test_not_found_route(void) {
    // Write initial config with just a basic website setup
    const char *config = 
        "website {\n"
        "    name \"404 Test Site\"\n"
        "    port 3456\n"
        "    database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
        "\n"
        "    page {\n"
        "        route \"/home\"\n"
        "        layout \"main\"\n"
        "        mustache {\n"
        "            <h1>Home</h1>\n"
        "        }\n"
        "    }\n"
        "}\n";
    
    writeConfig(config);
    
    // Parse and start server
    Parser parser = {0};
    WebsiteNode *website = reloadWebsite(&parser, NULL, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    
    // Give server time to start
    
    
    // Test non-existent route
    long response_code;
    char *headers = NULL;
    char *response_data = makePostRequest("http://localhost:3456/this-route-does-not-exist", 
                                        "", &response_code, &headers);
    
    // Verify 404 response
    TEST_ASSERT_EQUAL(404, response_code);
    TEST_ASSERT_NOT_NULL(response_data);
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>404 Not Found</h1>"));
    
    free(response_data);
    free(headers);
    
    // Also test with GET request to ensure both methods return 404
    response_data = makeRawRequest("http://localhost:3456/another-non-existent-route", NULL);
    TEST_ASSERT_NOT_NULL(response_data);
    TEST_ASSERT_NOT_NULL(strstr(response_data, "<h1>404 Not Found</h1>"));
    
    free(response_data);
    
    // Clean up
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
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
    RUN_TEST(test_page_post_handler);
    RUN_TEST(test_page_post_with_reference_data);
    RUN_TEST(test_page_redirect);
    RUN_TEST(test_route_params);
    RUN_TEST(test_json_post_endpoint);
    RUN_TEST(test_not_found_route);
    return UNITY_END();
}
