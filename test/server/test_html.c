#include "../../src/server/html.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>

// Function prototype
int run_server_html_tests(void);

static void test_generate_html_content_simple(void) {
    Arena *arena = createArena(1024 * 64);  // 64KB arena for this test
    
    // Create a simple content node
    ContentNode *node = arenaAlloc(arena, sizeof(ContentNode));
    node->type = arenaDupString(arena, "p");
    node->arg1 = arenaDupString(arena, "Hello, World!");
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
    TEST_ASSERT_TRUE(strstr(result, "Child content") != NULL);
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

static void test_generate_nested_html_content(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create a more complex nested structure
    ContentNode *grandchild = arenaAlloc(arena, sizeof(ContentNode));
    grandchild->type = arenaDupString(arena, "span");
    grandchild->arg1 = arenaDupString(arena, "Nested text");
    grandchild->arg2 = NULL;
    grandchild->children = NULL;
    grandchild->next = NULL;

    ContentNode *child = arenaAlloc(arena, sizeof(ContentNode));
    child->type = arenaDupString(arena, "p");
    child->arg1 = NULL;
    child->arg2 = NULL;
    child->children = grandchild;
    child->next = NULL;

    ContentNode *parent = arenaAlloc(arena, sizeof(ContentNode));
    parent->type = arenaDupString(arena, "div");
    parent->arg1 = NULL;
    parent->arg2 = NULL;
    parent->children = child;
    parent->next = NULL;

    char* result = generateHtmlContent(arena, parent, 0);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify structure
    TEST_ASSERT_TRUE(strstr(result, "<div>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "<p>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "<span>Nested text</span>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "</p>") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "</div>") != NULL);

    freeArena(arena);
}

int run_server_html_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_html_content_simple);
    RUN_TEST(test_generate_html_content_nested);
    RUN_TEST(test_generate_html_content_link);
    RUN_TEST(test_generate_html_content_image);
    RUN_TEST(test_generate_nested_html_content);
    return UNITY_END();
}
