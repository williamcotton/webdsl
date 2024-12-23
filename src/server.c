#include "server.h"
#include "stringbuilder.h"
#include "arena.h"
#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static WebsiteNode *currentWebsite = NULL;
static struct MHD_Daemon *httpd = NULL;
static Arena *serverArena = NULL;

char* generateHtmlContent(Arena *arena, const ContentNode *cn, int indent) {
    StringBuilder *sb = StringBuilder_new(arena);
    char indentStr[32];
    
    while (cn) {
        memset(indentStr, ' ', (size_t)(indent * 2));
        indentStr[indent * 2] = '\0';
        
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
            StringBuilder_append(sb, "  %s: %s;\n", prop->property, prop->value);
            prop = prop->next;
        }
        StringBuilder_append(sb, "}\n\n");
        styleHead = styleHead->next;
    }
    
    char* result = arenaDupString(arena, StringBuilder_get(sb));
    return result;
}

static void buildRouteMaps(WebsiteNode *website, Arena *arena) {
    // Build route map
    PageNode *page = website->pageHead;
    while (page) {
        RouteMap *rm = arenaAlloc(arena, sizeof(RouteMap));
        rm->page = page;
        rm->route = arenaDupString(arena, stripQuotes(page->route));
        rm->next = website->routeMap;
        website->routeMap = rm;
        page = page->next;
    }

    // Build layout map
    LayoutNode *layout = website->layoutHead;
    while (layout) {
        LayoutMap *lm = arenaAlloc(arena, sizeof(LayoutMap));
        lm->layout = layout;
        lm->identifier = arenaDupString(arena, layout->identifier);
        lm->next = website->layoutMap;
        website->layoutMap = lm;
        layout = layout->next;
    }
}

static PageNode* findPage(WebsiteNode *website, const char *url) {
    RouteMap *rm = website->routeMap;
    while (rm) {
        if (strcmp(rm->route, url) == 0) {
            return rm->page;
        }
        rm = rm->next;
    }
    return NULL;
}

static LayoutNode* findLayout(WebsiteNode *website, const char *identifier) {
    LayoutMap *lm = website->layoutMap;
    while (lm) {
        if (strcmp(lm->identifier, identifier) == 0) {
            return lm->layout;
        }
        lm = lm->next;
    }
    return NULL;
}

static enum MHD_Result requestHandler(void *cls __attribute__((unused)), struct MHD_Connection *connection,
                         const char *url, const char *method,
                         const char *version __attribute__((unused)),
                         const char *upload_data __attribute__((unused)),
                         size_t *upload_data_size __attribute__((unused)),
                         void **con_cls __attribute__((unused))) {
    Arena *request_arena = createArena(1024 * 1024);  // 1MB arena for this request
    if (strcmp(method, "GET") != 0) {
        freeArena(request_arena);
        return MHD_NO;
    }
    
    struct MHD_Response *response;
    enum MHD_Result ret;
    
    if (strcmp(url, "/styles.css") == 0) {
        char *css = generateCss(request_arena, currentWebsite->styleHead);
        char *css_copy = strdup(css);
        response = MHD_create_response_from_buffer(strlen(css_copy), css_copy,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/css");
    } else {
        // Find matching page
        PageNode *page = findPage(currentWebsite, url);
        
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
        LayoutNode *layout = findLayout(currentWebsite, page->layout);
        
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
    serverArena = arena;  // 1MB arena for maps
    buildRouteMaps(website, serverArena);  // Build lookup maps

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
    if (currentWebsite) {
        currentWebsite = NULL;
    }
}
