#include "html.h"
#include "routing.h"
#include "../stringbuilder.h"
#include <string.h>

extern Arena *serverArena;
extern WebsiteNode *currentWebsite;

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
                StringBuilder_append(sb, "%s<a href=\"%s\">%s</a>\n", 
                    indentStr, cn->arg1, cn->arg2);
            } else if (strcmp(cn->type, "image") == 0) {
                StringBuilder_append(sb, "%s<img src=\"%s\" alt=\"%s\"/>\n", 
                    indentStr, cn->arg1, cn->arg2 ? cn->arg2 : "");
            } else {
                StringBuilder_append(sb, "%s<%s>%s</%s>\n",
                    indentStr, cn->type, cn->arg1, cn->type);
            }
        }
        cn = cn->next;
    }
    
    return arenaDupString(arena, StringBuilder_get(sb));
}

char* generateFullHtml(Arena *arena, PageNode *page, LayoutNode *layout) {
    if (!layout) {
        // Create basic HTML structure with CSS for pages without layout
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "<!DOCTYPE html>\n<html>\n<head>\n");
        StringBuilder_append(sb, "<link rel=\"stylesheet\" href=\"/styles.css\">\n");
        StringBuilder_append(sb, "</head>\n<body>\n");
        char *content = generateHtmlContent(arena, page->contentHead, 1);
        StringBuilder_append(sb, "%s", content);
        StringBuilder_append(sb, "</body>\n</html>");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    char *layoutHtml = generateHtmlContent(arena, layout->bodyContent, 0);
    char *pageContent = generateHtmlContent(arena, page->contentHead, 0);
    
    // Add CSS link to head if not already present
    StringBuilder *sb = StringBuilder_new(arena);
    if (!strstr(layoutHtml, "<link rel=\"stylesheet\" href=\"/styles.css\">")) {
        StringBuilder_append(sb, "<!DOCTYPE html>\n<html>\n<head>\n");
        StringBuilder_append(sb, "<link rel=\"stylesheet\" href=\"/styles.css\">\n");
        StringBuilder_append(sb, "</head>\n<body>\n");
    }
    
    const char *placeholder = "{CONTENT_PLACEHOLDER}";
    const char *pos = strstr(layoutHtml, placeholder);
    
    if (pos) {
        size_t prefix_len = (size_t)(pos - layoutHtml);
        StringBuilder_append(sb, "%.*s", (int)prefix_len, layoutHtml);
        StringBuilder_append(sb, "%s", pageContent);
        StringBuilder_append(sb, "%s", pos + strlen(placeholder));
    } else {
        StringBuilder_append(sb, "%s", layoutHtml);
    }
    
    return arenaDupString(arena, StringBuilder_get(sb));
}

char* generateCss(Arena *arena, StyleBlockNode *styleHead) {
    StringBuilder *sb = StringBuilder_new(arena);
    
    for (StyleBlockNode *block = styleHead; block; block = block->next) {
        // Handle raw CSS blocks
        if (block->propHead && strcmp(block->propHead->property, "raw_css") == 0) {
            // Raw CSS blocks should be output directly without wrapping
            StringBuilder_append(sb, "%s\n", block->propHead->value);
        } else {
            // Regular property-value pairs
            StringBuilder_append(sb, "%s {\n", block->selector);
            for (StylePropNode *prop = block->propHead; prop; prop = prop->next) {
                StringBuilder_append(sb, "  %s: %s;\n", prop->property, prop->value);
            }
            StringBuilder_append(sb, "}\n\n");
        }
    }
    
    return arenaDupString(arena, StringBuilder_get(sb));
}

enum MHD_Result handlePageRequest(struct MHD_Connection *connection, const char *url, Arena *arena) {
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

    char *html = generateFullHtml(arena, page, layout);
    char *html_copy = strdup(html);
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(html_copy), html_copy,
                                               MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "text/html");
    return MHD_queue_response(connection, MHD_HTTP_OK, response);
}

enum MHD_Result handleCssRequest(struct MHD_Connection *connection, Arena *arena) {
    char *css = generateCss(arena, currentWebsite->styleHead);
    char *css_copy = strdup(css);
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(css_copy), css_copy,
                                               MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "text/css");
    return MHD_queue_response(connection, MHD_HTTP_OK, response);
}
