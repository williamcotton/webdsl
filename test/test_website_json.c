#include "../test/unity/unity.h"
#include "../src/website_json.h"
#include "../src/ast.h"
#include "../src/parser.h"
#include "test_runners.h"
#include <jansson.h>
#include <string.h>

// Function prototype
int run_website_json_tests(void);

static void test_website_to_json(void) {
    Parser parser;
    const char *input = 
        "website {\n"
        "  name \"Test Site\"\n"
        "  author \"Test Author\"\n"
        "  version \"1.0\"\n"
        "  port 3000\n"
        "  page {\n"
        "    name \"home\"\n"
        "    route \"/\"\n"
        "    layout \"main\"\n"
        "    error {\n"
        "      redirect \"/error\"\n"
        "    }\n"
        "    success {\n"
        "      mustache {\n"
        "        <div>Success!</div>\n"
        "      }\n"
        "    }\n"
        "    mustache {\n"
        "      <h1>Welcome</h1>\n"
        "    }\n"
        "  }\n"
        "}";
    
    initParser(&parser, input);
    WebsiteNode *website = parseProgram(&parser);
    
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL(0, parser.hadError);
    
    char *json = websiteToJson(parser.arena, website);
    TEST_ASSERT_NOT_NULL(json);
    
    // Parse the JSON string
    json_error_t jsonError;
    json_t *root = json_loads(json, 0, &jsonError);
    TEST_ASSERT_NOT_NULL(root);
    
    // Check basic website properties
    TEST_ASSERT_EQUAL_STRING("Test Site", json_string_value(json_object_get(root, "name")));
    TEST_ASSERT_EQUAL_STRING("Test Author", json_string_value(json_object_get(root, "author")));
    TEST_ASSERT_EQUAL_STRING("1.0", json_string_value(json_object_get(root, "version")));
    TEST_ASSERT_EQUAL(3000, json_integer_value(json_object_get(root, "port")));
    
    // Check pages array
    json_t *pages = json_object_get(root, "pages");
    TEST_ASSERT_NOT_NULL(pages);
    TEST_ASSERT_TRUE(json_is_array(pages));
    TEST_ASSERT_EQUAL(1, json_array_size(pages));
    
    // Check first page
    json_t *page = json_array_get(pages, 0);
    TEST_ASSERT_NOT_NULL(page);
    TEST_ASSERT_EQUAL_STRING("home", json_string_value(json_object_get(page, "name")));
    TEST_ASSERT_EQUAL_STRING("/", json_string_value(json_object_get(page, "route")));
    TEST_ASSERT_EQUAL_STRING("main", json_string_value(json_object_get(page, "layout")));
    
    // Check error block
    json_t *errorBlock = json_object_get(page, "error");
    TEST_ASSERT_NOT_NULL(errorBlock);
    TEST_ASSERT_EQUAL_STRING("/error", json_string_value(json_object_get(errorBlock, "redirect")));
    
    // Check success block
    json_t *success = json_object_get(page, "success");
    TEST_ASSERT_NOT_NULL(success);
    json_t *successTemplate = json_object_get(success, "template");
    TEST_ASSERT_NOT_NULL(successTemplate);
    TEST_ASSERT_TRUE(strstr(json_string_value(json_object_get(successTemplate, "content")), "<div>Success!</div>") != NULL);
    
    // Check main template
    json_t *template = json_object_get(page, "template");
    TEST_ASSERT_NOT_NULL(template);
    TEST_ASSERT_TRUE(strstr(json_string_value(json_object_get(template, "content")), "<h1>Welcome</h1>") != NULL);
    
    json_decref(root);
    free(json);
    freeArena(parser.arena);
}

int run_website_json_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_website_to_json);
    return UNITY_END();
}
