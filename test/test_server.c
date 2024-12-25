#include "../test/unity/unity.h"
#include "../src/server.h"
#include "../src/ast.h"
#include "../src/arena.h"
#include <string.h>
#include "test_runners.h"

// Function prototype
int run_server_tests(void);

static void test_strip_quotes(void) {
    // Test with quoted string
    const char* input1 = "\"hello\"";
    const char* result1 = stripQuotes(input1);
    TEST_ASSERT_EQUAL_STRING("hello", result1);

    // Test with unquoted string
    const char* input2 = "hello";
    const char* result2 = stripQuotes(input2);
    TEST_ASSERT_EQUAL_STRING("hello", result2);

    // Test with empty quoted string
    const char* input3 = "\"\"";
    const char* result3 = stripQuotes(input3);
    TEST_ASSERT_EQUAL_STRING("", result3);

    // Test with NULL
    const char* result4 = stripQuotes(NULL);
    TEST_ASSERT_NULL(result4);
}

static void test_generate_html_content_simple(void) {
    Arena *arena = createArena(1024 * 64);  // 64KB arena for this test
    
    // Create a simple content node
    ContentNode *node = arenaAlloc(arena, sizeof(ContentNode));
    node->type = arenaDupString(arena, "p");
    node->arg1 = arenaDupString(arena, "\"Hello, World!\"");
    node->arg2 = NULL;
    node->children = NULL;
    node->next = NULL;

    char* result = generateHtmlContent(arena, node, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("<p>Hello, World!</p>\n", result);

    freeArena(arena);
}

static void test_generate_html_content_nested(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create nested content nodes
    ContentNode *child = arenaAlloc(arena, sizeof(ContentNode));
    child->type = arenaDupString(arena, "p");
    child->arg1 = arenaDupString(arena, "\"Child content\"");
    child->arg2 = NULL;
    child->children = NULL;
    child->next = NULL;

    ContentNode *parent = arenaAlloc(arena, sizeof(ContentNode));
    parent->type = arenaDupString(arena, "div");
    parent->arg1 = NULL;
    parent->arg2 = NULL;
    parent->children = child;
    parent->next = NULL;

    char* result = generateHtmlContent(arena, parent, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "<div>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "<p>Child content</p>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "</div>") != NULL);

    freeArena(arena);
}

static void test_generate_html_content_link(void) {
    Arena *arena = createArena(1024 * 64);
    
    ContentNode *node = arenaAlloc(arena, sizeof(ContentNode));
    node->type = arenaDupString(arena, "link");
    node->arg1 = arenaDupString(arena, "/test");
    node->arg2 = arenaDupString(arena, "Test Link");
    node->children = NULL;
    node->next = NULL;

    char* result = generateHtmlContent(arena, node, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("<a href=\"/test\">Test Link</a>\n", result);

    freeArena(arena);
}

static void test_generate_css_simple(void) {
    Arena *arena = createArena(1024 * 64);
    
    StylePropNode *prop = arenaAlloc(arena, sizeof(StylePropNode));
    prop->property = arenaDupString(arena, "color");
    prop->value = arenaDupString(arena, "#000");
    prop->next = NULL;

    StyleBlockNode *block = arenaAlloc(arena, sizeof(StyleBlockNode));
    block->selector = arenaDupString(arena, "body");
    block->propHead = prop;
    block->next = NULL;

    char* result = generateCss(arena, block);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "body {") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "color: #000;") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "}") != NULL);

    freeArena(arena);
}

static void test_generate_css_multiple_properties(void) {
    Arena *arena = createArena(1024 * 64);
    
    StylePropNode *prop2 = arenaAlloc(arena, sizeof(StylePropNode));
    prop2->property = arenaDupString(arena, "margin");
    prop2->value = arenaDupString(arena, "0");
    prop2->next = NULL;

    StylePropNode *prop1 = arenaAlloc(arena, sizeof(StylePropNode));
    prop1->property = arenaDupString(arena, "color");
    prop1->value = arenaDupString(arena, "#000");
    prop1->next = prop2;

    StyleBlockNode *block = arenaAlloc(arena, sizeof(StyleBlockNode));
    block->selector = arenaDupString(arena, "body");
    block->propHead = prop1;
    block->next = NULL;

    char* result = generateCss(arena, block);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "color: #000;") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "margin: 0;") != NULL);

    freeArena(arena);
}

static void test_generate_html_content_image(void) {
    Arena *arena = createArena(1024 * 64);
    
    ContentNode *node = arenaAlloc(arena, sizeof(ContentNode));
    node->type = arenaDupString(arena, "image");
    node->arg1 = arenaDupString(arena, "/test.jpg");
    node->arg2 = arenaDupString(arena, "Test Image");
    node->children = NULL;
    node->next = NULL;

    char* result = generateHtmlContent(arena, node, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("<img src=\"/test.jpg\" alt=\"Test Image\"/>\n", result);

    freeArena(arena);
}

int run_server_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_strip_quotes);
    RUN_TEST(test_generate_html_content_simple);
    RUN_TEST(test_generate_html_content_nested);
    RUN_TEST(test_generate_html_content_link);
    RUN_TEST(test_generate_html_content_image);
    RUN_TEST(test_generate_css_simple);
    RUN_TEST(test_generate_css_multiple_properties);
    return UNITY_END();
}
