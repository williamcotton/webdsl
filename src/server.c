#include "server.h"
#include "stringbuilder.h"
#include "arena.h"
#include "db.h"
#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

static WebsiteNode *currentWebsite = NULL;
static struct MHD_Daemon *httpd = NULL;
static Arena *serverArena = NULL;
static Database *db = NULL;

#define HASH_TABLE_SIZE 64  // Should be power of 2
#define HASH_MASK (HASH_TABLE_SIZE - 1)

typedef struct RouteHashEntry {
    const char *route;
    PageNode *page;
    struct RouteHashEntry *next;  // For collision handling
} RouteHashEntry;

typedef struct LayoutHashEntry {
    const char *identifier;
    LayoutNode *layout;
    struct LayoutHashEntry *next;
} LayoutHashEntry;

typedef struct ApiHashEntry {
    const char *route;
    ApiEndpoint *endpoint;
    struct ApiHashEntry *next;
} ApiHashEntry;

typedef struct QueryHashEntry {
    const char *name;
    QueryNode *query;
    struct QueryHashEntry *next;
} QueryHashEntry;

// Add this to WebsiteNode struct in ast.h if you can, otherwise we'll work with the routeMap
static RouteHashEntry *routeTable[HASH_TABLE_SIZE];
static LayoutHashEntry *layoutTable[HASH_TABLE_SIZE];
static ApiHashEntry *apiTable[HASH_TABLE_SIZE];
static QueryHashEntry *queryTable[HASH_TABLE_SIZE];

// This will store the parsed POST data
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct PostData {
    char *values[32];  // Array of values matching the expected fields
    struct MHD_Connection *connection;// Put bitfield first
    size_t value_count;
    int error;
};
#pragma clang diagnostic pop

static enum MHD_Result post_iterator(void *cls,
                                   enum MHD_ValueKind kind,
                                   const char *key,
                                   const char *filename,
                                   const char *content_type,
                                   const char *transfer_encoding,
                                   const char *data,
                                   uint64_t off,
                                   size_t size) {
    (void)kind; (void)filename; (void)content_type; (void)key;
    (void)transfer_encoding; (void)off; (void)size;
    
    struct PostData *post_data = cls;
    
    // Store the value in our array
    if (post_data->value_count < 32) {
        post_data->values[post_data->value_count++] = strdup(data);
    }
    
    return MHD_YES;
}

struct PostContext {
    char *data;
    size_t size;
    size_t processed;
    struct MHD_PostProcessor *pp;
    struct PostData post_data;
};

char* generateHtmlContent(Arena *arena, const ContentNode *cn, int indent) {
    StringBuilder *sb = StringBuilder_new(arena);
    char indentStr[32];
    
    while (cn) {
        memset(indentStr, ' ', (size_t)(indent * 2));
        indentStr[indent * 2] = '\0';
        
        // Handle raw HTML content
        if (strcmp(cn->type, "raw_html") == 0) {
            const char *html = cn->arg1;
            const char *content_marker = "<!-- content -->";
            const char *marker_pos = strstr(html, content_marker);
            
            if (marker_pos) {
                // Write everything before the marker
                size_t prefix_len = (size_t)(marker_pos - html);
                StringBuilder_append(sb, "%s%.*s", indentStr, (int)prefix_len, html);
                
                // Insert the content placeholder
                StringBuilder_append(sb, "{CONTENT_PLACEHOLDER}");
                
                // Write everything after the marker
                const char *suffix = marker_pos + strlen(content_marker);
                StringBuilder_append(sb, "%s", suffix);
            } else {
                StringBuilder_append(sb, "%s%s\n", indentStr, html);
            }
            cn = cn->next;
            continue;
        }

        if (cn->children) {
            // Generic tag with children
            StringBuilder_append(sb, "%s<%s>\n", indentStr, cn->type);
            char* childContent = generateHtmlContent(arena, cn->children, indent + 1);
            StringBuilder_append(sb, "%s", childContent);
            StringBuilder_append(sb, "%s</%s>\n", indentStr, cn->type);
        } else {
            if (strcmp(cn->type, "content") == 0) {
                StringBuilder_append(sb, "%s{CONTENT_PLACEHOLDER}\n", indentStr);
            } else if (strcmp(cn->type, "link") == 0) {
                // Special case for links since they have href attribute
                StringBuilder_append(sb, "%s<a href=\"%s\">%s</a>\n", 
                    indentStr, cn->arg1, cn->arg2);
            } else if (strcmp(cn->type, "image") == 0) {
                // Special case for images since they're self-closing with src/alt
                StringBuilder_append(sb, "%s<img src=\"%s\" alt=\"%s\"/>\n", 
                    indentStr, cn->arg1, cn->arg2 ? cn->arg2 : "");
            } else {
                // No need to strip quotes anymore
                StringBuilder_append(sb, "%s<%s>%s</%s>\n",
                    indentStr, cn->type, cn->arg1, cn->type);
            }
        }
        cn = cn->next;
    }
    
    char* result = arenaDupString(arena, StringBuilder_get(sb));
    return result;
}

static char* generateFullHtml(Arena *arena, PageNode *page, LayoutNode *layout) {
    if (!layout) {
        fprintf(stderr, "Error: Layout not found for page '%s'\n", page->identifier);
        return arenaDupString(arena, "<html><body><h1>500 Internal Server Error</h1><p>Layout not found</p></body></html>");
    }
    
    StringBuilder *sb = StringBuilder_new(arena);
    
    // Add doctype and open html tag
    StringBuilder_append(sb, "%s\n<html>\n", layout->doctype ? layout->doctype : "<!DOCTYPE html>");
    
    // Head section
    StringBuilder_append(sb, "<head>\n");
    if (page->title) {
        StringBuilder_append(sb, "  <title>%s</title>\n", page->title);
    }
    if (page->description) {
        StringBuilder_append(sb, "  <meta name=\"description\" content=\"%s\">\n", page->description);
    }
    StringBuilder_append(sb, "  <link rel=\"stylesheet\" href=\"/styles.css\">\n");
    if (layout->headContent) {
        StringBuilder_append(sb, generateHtmlContent(arena, layout->headContent, 1));
    }
    StringBuilder_append(sb, "</head>\n");
    
    // Body section
    StringBuilder_append(sb, "<body>\n");
    if (layout->bodyContent) {
        char *layout_content = generateHtmlContent(arena, layout->bodyContent, 1);
        char *page_content = generateHtmlContent(arena, page->contentHead, 1);
        char *content_pos = strstr(layout_content, "{CONTENT_PLACEHOLDER}");
        if (content_pos) {
            *content_pos = '\0';  // Split the layout content
            StringBuilder_append(sb, "%s", layout_content);  // Before content
            StringBuilder_append(sb, "%s", page_content);    // Page content
            StringBuilder_append(sb, "%s", content_pos + strlen("{CONTENT_PLACEHOLDER}")); // After content
        } else {
            StringBuilder_append(sb, "%s", layout_content);
            StringBuilder_append(sb, "%s", page_content);    // Append content at the end if no placeholder
        }
    }
    StringBuilder_append(sb, "</body>\n");
    
    StringBuilder_append(sb, "</html>");
    
    char* result = arenaDupString(arena, StringBuilder_get(sb));
    return result;
}

char* generateCss(Arena *arena, StyleBlockNode *styleHead) {
    StringBuilder *sb = StringBuilder_new(arena);
    
    while (styleHead) {
        StringBuilder_append(sb, "%s {\n", styleHead->selector);
        StylePropNode *prop = styleHead->propHead;
        while (prop) {
            // No need to strip quotes anymore
            StringBuilder_append(sb, "  %s: %s;\n", prop->property, prop->value);
            prop = prop->next;
        }
        StringBuilder_append(sb, "}\n\n");
        styleHead = styleHead->next;
    }
    
    char* result = arenaDupString(arena, StringBuilder_get(sb));
    return result;
}

static uint32_t hashString(const char *str) __attribute__((no_sanitize("unsigned-integer-overflow"))) {
    uint32_t hash = 0;
    
    for (size_t i = 0; str[i] != '\0'; i++) {
        hash = (hash * 31 + (unsigned char)str[i]) % UINT32_MAX;
    }
    
    return hash;
}

static void insertRoute(const char *route, PageNode *page, Arena *arena) {
    uint32_t hash = hashString(route) & HASH_MASK;
    RouteHashEntry *entry = arenaAlloc(arena, sizeof(RouteHashEntry));
    entry->route = route;
    entry->page = page;
    entry->next = routeTable[hash];
    routeTable[hash] = entry;
}

static void insertLayout(const char *identifier, LayoutNode *layout, Arena *arena) {
    uint32_t hash = hashString(identifier) & HASH_MASK;
    LayoutHashEntry *entry = arenaAlloc(arena, sizeof(LayoutHashEntry));
    entry->identifier = identifier;
    entry->layout = layout;
    entry->next = layoutTable[hash];
    layoutTable[hash] = entry;
}

static void insertApi(const char *route, ApiEndpoint *endpoint, Arena *arena) {
    uint32_t hash = hashString(route) & HASH_MASK;
    ApiHashEntry *entry = arenaAlloc(arena, sizeof(ApiHashEntry));
    entry->route = route;
    entry->endpoint = endpoint;
    entry->next = apiTable[hash];
    apiTable[hash] = entry;
}

static void insertQuery(const char *name, QueryNode *query, Arena *arena) {
    uint32_t hash = hashString(name) & HASH_MASK;
    QueryHashEntry *entry = arenaAlloc(arena, sizeof(QueryHashEntry));
    entry->name = name;
    entry->query = query;
    entry->next = queryTable[hash];
    queryTable[hash] = entry;
}

static void buildRouteMaps(WebsiteNode *website, Arena *arena) {
    // Initialize hash tables
    memset(routeTable, 0, sizeof(routeTable));
    memset(layoutTable, 0, sizeof(layoutTable));
    memset(apiTable, 0, sizeof(apiTable));
    memset(queryTable, 0, sizeof(queryTable));
    
    // Build route hash table
    PageNode *page = website->pageHead;
    while (page) {
        const char *route = arenaDupString(arena, page->route);
        insertRoute(route, page, arena);
        page = page->next;
    }

    // Build layout hash table
    LayoutNode *layout = website->layoutHead;
    while (layout) {
        const char *identifier = arenaDupString(arena, layout->identifier);
        insertLayout(identifier, layout, arena);
        layout = layout->next;
    }

    // Build API hash table
    ApiEndpoint *api = website->apiHead;
    while (api) {
        const char *route = arenaDupString(arena, api->route);
        insertApi(route, api, arena);
        api = api->next;
    }

    // Build query hash table
    QueryNode *query = website->queryHead;
    while (query) {
        const char *name = arenaDupString(arena, query->name);
        insertQuery(name, query, arena);
        query = query->next;
    }
}

static PageNode* findPage(const char *url) {
    uint32_t hash = hashString(url) & HASH_MASK;
    RouteHashEntry *entry = routeTable[hash];

    while (entry) {
        if (strcmp(entry->route, url) == 0) {
            return entry->page;
        }
        entry = entry->next;
    }
    return NULL;
}

static LayoutNode* findLayout(const char *identifier) {
    uint32_t hash = hashString(identifier) & HASH_MASK;
    LayoutHashEntry *entry = layoutTable[hash];
    
    while (entry) {
        if (strcmp(entry->identifier, identifier) == 0) {
            return entry->layout;
        }
        entry = entry->next;
    }
    return NULL;
}

static ApiEndpoint* findApi(const char *url) {
    uint32_t hash = hashString(url) & HASH_MASK;
    ApiHashEntry *entry = apiTable[hash];
    
    while (entry) {
        if (strcmp(entry->route, url) == 0) {
            return entry->endpoint;
        }
        entry = entry->next;
    }
    return NULL;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
static QueryNode* findQuery(const char *name) {
    uint32_t hash = hashString(name) & HASH_MASK;
    QueryHashEntry *entry = queryTable[hash];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->query;
        }
        entry = entry->next;
    }
    return NULL;
}
#pragma clang diagnostic pop

static char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls) {
    // For POST requests with form data
    if (strcmp(endpoint->method, "POST") == 0 && endpoint->fields) {
        // Skip if this is a GET request marker
        if (con_cls == &"GET") {
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Invalid request type\"\n");
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }
        
        struct PostContext *post_ctx = con_cls;
        if (!post_ctx) {
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Missing POST context\"\n");
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }

        // Build array of values from form data
        const char **values = arenaAlloc(arena, sizeof(char*) * 32);  // Max 32 fields
        size_t value_count = 0;
        
        // Extract each field from form data
        ResponseField *field = endpoint->fields;
        size_t field_index = 0;
        while (field) {
            const char *value = NULL;
            if (field_index < post_ctx->post_data.value_count) {
                value = post_ctx->post_data.values[field_index];
                field_index++;
            }

            if (!value) {
                StringBuilder *sb = StringBuilder_new(arena);
                StringBuilder_append(sb, "{\n");
                StringBuilder_append(sb, "  \"error\": \"Missing required field: %s\"\n", field->name);
                StringBuilder_append(sb, "}");
                return arenaDupString(arena, StringBuilder_get(sb));
            }
            values[value_count++] = value;
            field = field->next;
        }

        // Execute parameterized query
        QueryNode *query = findQuery(endpoint->response);
        if (!query) {
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Query not found: %s\"\n", endpoint->response);
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }

        PGresult *result = executeParameterizedQuery(db, query->sql, values, value_count);
        if (!result) {
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Query execution failed: %s\"\n", getDatabaseError(db));
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }

        char *json = resultToJson(arena, result);
        freeResult(result);
        return json;
    }

    // Regular GET request handling...
    QueryNode *query = findQuery(endpoint->response);
    
    if (!query) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Query not found: %s\"\n", endpoint->response);
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    // Execute the query (no need to strip quotes)
    PGresult *result = executeQuery(db, query->sql);
    if (!result) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Query execution failed: %s\"\n", 
                           getDatabaseError(db));
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    // Convert result to JSON
    char *json = resultToJson(arena, result);
    freeResult(result);

    if (!json) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Failed to convert result to JSON\"\n");
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    return json;
}

static enum MHD_Result requestHandler(void *cls __attribute__((unused)), struct MHD_Connection *connection,
                         const char *url, const char *method,
                         const char *version __attribute__((unused)),
                         const char *upload_data,
                         size_t *upload_data_size,
                         void **con_cls) {
    // First call for this connection
    if (*con_cls == NULL) {
        if (strcmp(method, "POST") == 0) {
            struct PostContext *post = malloc(sizeof(struct PostContext));
            post->data = NULL;
            post->size = 0;
            post->processed = 0;
            post->post_data.connection = connection;
            post->post_data.error = 0;
            post->post_data.value_count = 0;  // Initialize value count
            // Create post processor with our iterator
            post->pp = MHD_create_post_processor(connection,
                                                32 * 1024,  // 32k buffer
                                                post_iterator,
                                                &post->post_data);
            if (!post->pp) {
                free(post);
                return MHD_NO;
            }
            *con_cls = post;
            return MHD_YES;
        }
        *con_cls = &"GET";  // Just a non-NULL marker for GET requests
        return MHD_YES;
    }

    // Handle POST data
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post = *con_cls;
        
        if (*upload_data_size != 0) {
            if (MHD_post_process(post->pp, upload_data, *upload_data_size) == MHD_NO) {
                return MHD_NO;
            }
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        // Check if we had any errors during processing
        if (post->post_data.error) {
            return MHD_NO;
        }
    }

    // Check for API endpoint first
    ApiEndpoint *api = findApi(url);
    if (api) {
        // Verify HTTP method matches
        if (strcmp(method, api->method) != 0) {
            const char *method_not_allowed = "{ \"error\": \"Method not allowed\" }";
            char *error = strdup(method_not_allowed);
            struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error), error,
                                                     MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(response, "Content-Type", "application/json");
            enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
            MHD_destroy_response(response);
            return ret;
        }

        // Generate API response with form data
        char *json = generateApiResponse(serverArena, api, *con_cls);
        char *json_copy = strdup(json);
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(json_copy), json_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        
        // Add CORS headers for API endpoints
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        
        return MHD_queue_response(connection, MHD_HTTP_OK, response);
    }

    // Handle regular pages and CSS as before
    if (strcmp(method, "GET") != 0) {
        return MHD_NO;
    }

    if (strcmp(url, "/styles.css") == 0) {
        char *css = generateCss(serverArena, currentWebsite->styleHead);
        char *css_copy = strdup(css);
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(css_copy), css_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/css");
        return MHD_queue_response(connection, MHD_HTTP_OK, response);
    } else {
        // Find matching page
        PageNode *page = findPage(url);

        if (!page) {
            const char *not_found_text = "<html><body><h1>404 Not Found</h1></body></html>";
            char *not_found = strdup(not_found_text);
            struct MHD_Response *response = MHD_create_response_from_buffer(strlen(not_found),
                                                     not_found,
                                                     MHD_RESPMEM_MUST_FREE);
            enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            return ret;
        }

        // Find layout
        LayoutNode *layout = findLayout(page->layout);

        char *html = generateFullHtml(serverArena, page, layout);
        char *html_copy = strdup(html);
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(html_copy), html_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/html");
        return MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
}

static void request_completed_callback(void *cls,
                                     struct MHD_Connection *connection,
                                     void **con_cls,
                                     enum MHD_RequestTerminationCode toe) {
    (void)cls; (void)connection; (void)toe;
    
    if (*con_cls != &"GET") {  // Check if it's not our GET marker
        struct PostContext *post = *con_cls;
        if (post) {
            if (post->data)
                free(post->data);
            if (post->pp)
                MHD_destroy_post_processor(post->pp);
            // Free stored values
            for (size_t i = 0; i < post->post_data.value_count; i++) {
                free(post->post_data.values[i]);
            }
            free(post);
        }
        *con_cls = NULL;
    }
}

void startServer(WebsiteNode *website, Arena *arena) {
    currentWebsite = website;
    serverArena = arena;
    buildRouteMaps(website, serverArena);

    // Initialize database connection
    if (website->databaseUrl) {
        db = initDatabase(serverArena, website->databaseUrl);
    }
    if (!db) {
        fprintf(stderr, "Failed to connect to database: %s\n", 
               website->databaseUrl ? website->databaseUrl : "no database URL configured");
        exit(1);
    }

    // Get port number from website definition, default to 8080 if not specified
    uint16_t port = website->port > 0 ? (uint16_t)website->port : 8080;

    httpd = MHD_start_daemon(MHD_USE_POLL_INTERNAL_THREAD | MHD_USE_INTERNAL_POLLING_THREAD, port,
                            NULL, NULL, &requestHandler, NULL,
                            MHD_OPTION_CONNECTION_TIMEOUT, 30,
                            MHD_OPTION_THREAD_POOL_SIZE, 4,
                            MHD_OPTION_NOTIFY_COMPLETED, request_completed_callback, NULL,
                            MHD_OPTION_END);
    if (httpd == NULL) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        exit(1);
    }

    printf("Server started on port %d\n", port);
}

void stopServer(void) {
    if (httpd) {
        MHD_stop_daemon(httpd);
        httpd = NULL;
    }
    if (db) {
        closeDatabase(db);
        db = NULL;
    }
    if (currentWebsite) {
        currentWebsite = NULL;
    }
}
