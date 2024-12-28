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

char* generateHtmlContent(Arena *arena, const ContentNode *cn, int indent) {
    StringBuilder *sb = StringBuilder_new(arena);
    char indentStr[32];
    
    while (cn) {
        memset(indentStr, ' ', (size_t)(indent * 2));
        indentStr[indent * 2] = '\0';
        
        // Handle raw HTML content
        if (strcmp(cn->type, "raw_html") == 0) {
            const char *html = stripQuotes(cn->arg1);
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
                // Generic tag with content
                StringBuilder_append(sb, "%s<%s>%s</%s>\n",
                    indentStr, cn->type, stripQuotes(cn->arg1), cn->type);
            }
        }
        cn = cn->next;
    }
    
    char* result = arenaDupString(arena, StringBuilder_get(sb));
    return result;
}

const char* stripQuotes(const char* str) {
    if (str && str[0] == '"' && str[strlen(str)-1] == '"') {
        static char buffer[1024];  // Static buffer for the stripped string
        size_t len = strlen(str) - 2;  // -2 for quotes
        memcpy(buffer, str + 1, len);
        buffer[len] = '\0';
        return buffer;
    }
    return str;
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
            // Strip quotes from the property value
            const char* cleanValue = stripQuotes(prop->value);
            StringBuilder_append(sb, "  %s: %s;\n", prop->property, cleanValue);
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
        const char *route = arenaDupString(arena, stripQuotes(page->route));
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
        const char *route = arenaDupString(arena, stripQuotes(api->route));
        insertApi(route, api, arena);
        api = api->next;
    }

    // Build query hash table
    QueryNode *query = website->queryHead;
    while (query) {
        const char *name = arenaDupString(arena, stripQuotes(query->name));
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

static char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint) {
    // Look up the query by name (strip quotes from response name)
    const char *queryName = stripQuotes(endpoint->response);
    QueryNode *query = findQuery(queryName);
    
    if (!query) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Query not found: %s\"\n", queryName);
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    // Execute the query
    PGresult *result = executeQuery(db, stripQuotes(query->sql));
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
                         const char *upload_data __attribute__((unused)),
                         size_t *upload_data_size __attribute__((unused)),
                         void **con_cls __attribute__((unused))) {
    Arena *request_arena = createArena(1024 * 1024);  // 1MB arena for this request
    struct MHD_Response *response;
    enum MHD_Result ret;

    // Check for API endpoint first
    ApiEndpoint *api = findApi(url);
    if (api) {
        // Verify HTTP method matches
        if (strcmp(method, stripQuotes(api->method)) != 0) {
            const char *method_not_allowed = "{ \"error\": \"Method not allowed\" }";
            char *error = strdup(method_not_allowed);
            response = MHD_create_response_from_buffer(strlen(error), error,
                                                     MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
            MHD_destroy_response(response);
            freeArena(request_arena);
            return ret;
        }

        // Generate API response
        char *json = generateApiResponse(request_arena, api);
        char *json_copy = strdup(json);
        response = MHD_create_response_from_buffer(strlen(json_copy), json_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        freeArena(request_arena);
        return ret;
    }

    // Handle regular pages and CSS as before
    if (strcmp(method, "GET") != 0) {
        freeArena(request_arena);
        return MHD_NO;
    }

    if (strcmp(url, "/styles.css") == 0) {
        char *css = generateCss(request_arena, currentWebsite->styleHead);
        char *css_copy = strdup(css);
        response = MHD_create_response_from_buffer(strlen(css_copy), css_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/css");
    } else {
        // Find matching page
        PageNode *page = findPage(url);

        if (!page) {
            const char *not_found_text = "<html><body><h1>404 Not Found</h1></body></html>";
            char *not_found = strdup(not_found_text);
            response = MHD_create_response_from_buffer(strlen(not_found),
                                                     not_found,
                                                     MHD_RESPMEM_MUST_FREE);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            freeArena(request_arena);
            return ret;
        }

        // Find layout
        LayoutNode *layout = findLayout(page->layout);

        char *html = generateFullHtml(request_arena, page, layout);
        char *html_copy = strdup(html);
        response = MHD_create_response_from_buffer(strlen(html_copy), html_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/html");
    }
    
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    freeArena(request_arena);
    return ret;
}

void startServer(WebsiteNode *website, Arena *arena) {
    currentWebsite = website;
    serverArena = arena;
    buildRouteMaps(website, serverArena);

    // Initialize database connection
    if (website->databaseUrl) {
        db = initDatabase(serverArena, stripQuotes(website->databaseUrl));
    }
    if (!db) {
        fprintf(stderr, "Failed to connect to database: %s\n", 
               website->databaseUrl ? stripQuotes(website->databaseUrl) : "no database URL configured");
        exit(1);
    }

    // Get port number from website definition, default to 8080 if not specified
    uint16_t port = website->port > 0 ? (uint16_t)website->port : 8080;

    httpd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, port,
                            NULL, NULL, &requestHandler, NULL,
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
