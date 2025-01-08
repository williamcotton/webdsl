#include "../test/unity/unity.h"
#include "../src/server/server.h"
#include "../src/parser.h"
#include "../src/file_utils.h"
#include "../src/website.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

// Function prototype
int run_website_tests(void);

// Test configuration files
static const char *TEST_CONFIG_1 = 
"website {\n"
"  name \"Test Site 1\"\n"
"  port 3001\n"
"  database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
"  api {\n"
"    route \"/test\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      jq {\n"
"        { message: \"config 1\" }\n"
"      }\n"
"    }\n"
"  }\n"
"}\n";

static const char *TEST_CONFIG_2 = 
"website {\n"
"  name \"Test Site 2\"\n"
"  port 3002\n"
"  database \"postgresql://localhost/express-test?gssencmode=disable\"\n"
"  api {\n"
"    route \"/test\"\n"
"    method \"GET\"\n"
"    pipeline {\n"
"      jq {\n"
"        { message: \"config 2\" }\n"
"      }\n"
"    }\n"
"  }\n"
"}\n";

static const char *TEST_FILE = "test_config.webdsl";

static void writeConfig(const char *config) {
    FILE *f = fopen(TEST_FILE, "w");
    TEST_ASSERT_NOT_NULL(f);
    size_t written = (size_t)fprintf(f, "%s", config);
    TEST_ASSERT_EQUAL(strlen(config), written);
    fclose(f);
    // Ensure the write is flushed to disk
    sync();

    // Verify file was written correctly
    char *contents = readFile(TEST_FILE);
    TEST_ASSERT_NOT_NULL(contents);
    TEST_ASSERT_EQUAL_STRING(config, contents);
    free(contents);
}

static void test_config_parsing(void) {
    // Write initial config
    writeConfig(TEST_CONFIG_1);
    
    // Test first load
    Parser parser = {0};
    WebsiteNode *website = NULL;
    website = reloadWebsite(&parser, website, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("Test Site 1", website->name);
    
    // Sleep briefly to ensure server is running
    usleep(100000);
    
    // Write new config and reload
    writeConfig(TEST_CONFIG_2);
    website = reloadWebsite(&parser, website, TEST_FILE);
    TEST_ASSERT_NOT_NULL(website);
    TEST_ASSERT_EQUAL_STRING("Test Site 2", website->name);
    
    // Final cleanup
    stopServer();
    freeArena(parser.arena);
    remove(TEST_FILE);
}

int run_website_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_config_parsing);
    return UNITY_END();
}
