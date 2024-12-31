#include "../../src/server/html.h"
#include "../../src/ast.h"
#include "../../src/arena.h"
#include "../unity/unity.h"
#include "../test_runners.h"
#include <string.h>

// Function prototype
int run_server_css_tests(void);

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

static void test_generate_css_with_multiple_blocks(void) {
    Arena *arena = createArena(1024 * 64);
    
    // Create multiple style blocks
    StylePropNode *bodyProps = arenaAlloc(arena, sizeof(StylePropNode));
    bodyProps->property = arenaDupString(arena, "margin");
    bodyProps->value = arenaDupString(arena, "0");
    bodyProps->next = arenaAlloc(arena, sizeof(StylePropNode));
    bodyProps->next->property = arenaDupString(arena, "padding");
    bodyProps->next->value = arenaDupString(arena, "20px");
    bodyProps->next->next = NULL;

    StyleBlockNode *bodyBlock = arenaAlloc(arena, sizeof(StyleBlockNode));
    bodyBlock->selector = arenaDupString(arena, "body");
    bodyBlock->propHead = bodyProps;
    
    StylePropNode *headerProps = arenaAlloc(arena, sizeof(StylePropNode));
    headerProps->property = arenaDupString(arena, "color");
    headerProps->value = arenaDupString(arena, "#333");
    headerProps->next = NULL;

    StyleBlockNode *headerBlock = arenaAlloc(arena, sizeof(StyleBlockNode));
    headerBlock->selector = arenaDupString(arena, "h1");
    headerBlock->propHead = headerProps;
    headerBlock->next = NULL;
    
    bodyBlock->next = headerBlock;

    char* result = generateCss(arena, bodyBlock);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify both blocks are present
    TEST_ASSERT_TRUE(strstr(result, "body {") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "margin: 0;") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "padding: 20px;") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "h1 {") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "color: #333;") != NULL);

    freeArena(arena);
}

int run_server_css_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_css_simple);
    RUN_TEST(test_generate_css_multiple_properties);
    RUN_TEST(test_generate_css_with_multiple_blocks);
    return UNITY_END();
}
