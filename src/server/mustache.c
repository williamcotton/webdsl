#include "mustache.h"
#include "routing.h"
#include "../stringbuilder.h"
#include <string.h>
#include <jansson.h>
#include "../deps/mustach/mustach-jansson.h"

static ServerContext *serverCtx = NULL;

void initMustache(ServerContext *_serverCtx) {
    serverCtx = _serverCtx;
}

char* generateTemplateContent(Arena *arena, const TemplateNode *template, int indent) {
    if (!template) return NULL;
    
    StringBuilder *sb = StringBuilder_new(arena);
    char indentStr[32];
    memset(indentStr, ' ', (size_t)(indent * 2));
    indentStr[indent * 2] = '\0';
    
    StringBuilder_append(sb, "%s%s", indentStr, template->content);
    
    return arenaDupString(arena, StringBuilder_get(sb));
}

char *generateFullPage(Arena *arena,
                      PageNode *page,
                      LayoutNode *layout,
                      json_t *pipelineResult) {                             
    StringBuilder *sb = StringBuilder_new(arena);

    // Generate layout and page content
    if (!layout) {
        // Just use the page content directly
        char *content = generateTemplateContent(arena, page->template, 0);
        if (content) {
            StringBuilder_append(sb, "%s", content);
        }
    } else {
        // Generate layout and page content
        char *layoutHtml = generateTemplateContent(arena, layout->bodyTemplate, 0);
        char *pageContent = generateTemplateContent(arena, page->template, 0);

        if (layoutHtml) {
            // Replace content placeholder in layout with page content
            const char *placeholder = "<!-- content -->";
            const char *pos = strstr(layoutHtml, placeholder);

            if (pos) {
                size_t prefix_len = (size_t)(pos - layoutHtml);
                StringBuilder_append(sb, "%.*s", (int)prefix_len, layoutHtml);
                if (pageContent) {
                    StringBuilder_append(sb, "%s", pageContent);
                }
                StringBuilder_append(sb, "%s", pos + strlen(placeholder));
            } else {
                StringBuilder_append(sb, "%s", layoutHtml);
                if (pageContent) {
                    StringBuilder_append(sb, "%s", pageContent);
                }
            }
        } else if (pageContent) {
            StringBuilder_append(sb, "%s", pageContent);
        }
    }

    // Get the template string
    const char *template = StringBuilder_get(sb);

    // Use passed-in pipeline result or create empty object
    json_t *data = pipelineResult;
    if (!data) {
        data = json_object();
    } else if (json_object_get(data, "error")) {
        // If we got an error response, create an empty object instead
        json_decref(data);
        data = json_object();
    }

    // Render the template with mustache
    char *result = NULL;
    size_t result_size = 0;
    int rc = mustach_jansson_mem(template, strlen(template), data,
                                Mustach_With_AllExtensions, &result, &result_size);

    // Clean up JSON data if we created it
    if (!pipelineResult || json_object_get(pipelineResult, "error")) {
        json_decref(data);
    }

    if (rc != MUSTACH_OK) {
        // Return the unprocessed template if mustache fails
        return arenaDupString(arena, template);
    }

    // Copy the result to the arena and free the original
    char *arena_result = arenaDupString(arena, result);
    free(result);

    return arena_result;
}

enum MHD_Result handleMustachePageRequest(struct MHD_Connection *connection,
                                        const char *url, Arena *arena,
                                        json_t *pipelineResult) {
    // Find matching page
    RouteParams params = {0};
    PageNode *page = findPage(url, &params, arena);

    if (!page) {
        const char *not_found_text =
            "<html><body><h1>404 Not Found</h1></body></html>";
        char *not_found = strdup(not_found_text);
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(not_found), not_found, MHD_RESPMEM_MUST_FREE);
        enum MHD_Result ret =
            MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Find layout
    LayoutNode *layout = findLayout(page->layout);

    // Generate the page with templates
    char *html = generateFullPage(arena, page, layout, pipelineResult);

    if (page->redirect) {
        struct MHD_Response *response =
            MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Location", page->redirect);
        enum MHD_Result ret =
            MHD_queue_response(connection, MHD_HTTP_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    char *html_copy = strdup(html);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(html_copy), html_copy, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "text/html");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

