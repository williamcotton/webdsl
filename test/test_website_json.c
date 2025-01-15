#include "unity/unity.h"
#include <string.h>
#include "../src/website_json.h"
#include "../src/ast.h"
#include "../src/arena.h"
#include <jansson.h>

static void test_website_to_json(void) {
    Arena *arena = createArena(1024 * 1024);  // 1MB arena
    
    // Create a test website
    WebsiteNode *website = arenaAlloc(arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    website->name = arenaDupString(arena, "Test Website");
    website->author = arenaDupString(arena, "Test Author");
    website->version = arenaDupString(arena, "1.0.0");
    website->port = 8080;
    
    // Create a test page
    PageNode *page = arenaAlloc(arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode));
    page->identifier = arenaDupString(arena, "home");
    page->route = arenaDupString(arena, "/");
    page->layout = arenaDupString(arena, "main");
    
    // Create a test template for the page
    TemplateNode *pageTemplate = arenaAlloc(arena, sizeof(TemplateNode));
    memset(pageTemplate, 0, sizeof(TemplateNode));
    pageTemplate->type = TEMPLATE_HTML;
    pageTemplate->content = arenaDupString(arena, "<h1>Welcome</h1>");
    page->template = pageTemplate;
    
    website->pageHead = page;
    
    // Create a test layout
    LayoutNode *layout = arenaAlloc(arena, sizeof(LayoutNode));
    memset(layout, 0, sizeof(LayoutNode));
    layout->identifier = arenaDupString(arena, "main");
    
    // Create a test template for the layout
    TemplateNode *layoutTemplate = arenaAlloc(arena, sizeof(TemplateNode));
    memset(layoutTemplate, 0, sizeof(TemplateNode));
    layoutTemplate->type = TEMPLATE_HTML;
    layoutTemplate->content = arenaDupString(arena, "<div>{{content}}</div>");
    layout->bodyTemplate = layoutTemplate;
    
    website->layoutHead = layout;
    
    // Convert to JSON and verify
    char *json = websiteToJson(website);
    TEST_ASSERT_NOT_NULL(json);
    
    json_error_t error;
    json_t *root = json_loads(json, 0, &error);
    TEST_ASSERT_NOT_NULL(root);
    
    // Verify website properties
    json_t *name = json_object_get(root, "name");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("Test Website", json_string_value(name));
    
    json_t *author = json_object_get(root, "author");
    TEST_ASSERT_NOT_NULL(author);
    TEST_ASSERT_EQUAL_STRING("Test Author", json_string_value(author));
    
    json_t *version = json_object_get(root, "version");
    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_EQUAL_STRING("1.0.0", json_string_value(version));
    
    json_t *port = json_object_get(root, "port");
    TEST_ASSERT_NOT_NULL(port);
    TEST_ASSERT_EQUAL_INT(8080, json_integer_value(port));
    
    // Verify pages
    json_t *pages = json_object_get(root, "pages");
    TEST_ASSERT_NOT_NULL(pages);
    TEST_ASSERT_TRUE(json_is_array(pages));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(pages));
    
    json_t *page_obj = json_array_get(pages, 0);
    TEST_ASSERT_NOT_NULL(page_obj);
    
    json_t *identifier = json_object_get(page_obj, "identifier");
    TEST_ASSERT_NOT_NULL(identifier);
    TEST_ASSERT_EQUAL_STRING("home", json_string_value(identifier));
    
    json_t *route = json_object_get(page_obj, "route");
    TEST_ASSERT_NOT_NULL(route);
    TEST_ASSERT_EQUAL_STRING("/", json_string_value(route));
    
    json_t *layout_ref = json_object_get(page_obj, "layout");
    TEST_ASSERT_NOT_NULL(layout_ref);
    TEST_ASSERT_EQUAL_STRING("main", json_string_value(layout_ref));
    
    // Verify layouts
    json_t *layouts = json_object_get(root, "layouts");
    TEST_ASSERT_NOT_NULL(layouts);
    TEST_ASSERT_TRUE(json_is_array(layouts));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(layouts));
    
    json_t *layout_obj = json_array_get(layouts, 0);
    TEST_ASSERT_NOT_NULL(layout_obj);
    
    json_t *layout_id = json_object_get(layout_obj, "identifier");
    TEST_ASSERT_NOT_NULL(layout_id);
    TEST_ASSERT_EQUAL_STRING("main", json_string_value(layout_id));
    
    // Clean up
    json_decref(root);
    free(json);
    freeArena(arena);
}

int run_website_json_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_website_to_json);
    return UNITY_END();
}
