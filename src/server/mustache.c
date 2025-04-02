#include "mustache.h"
#include "routing.h"
#include "../stringbuilder.h"
#include <string.h>
#include <jansson.h>
#include "../deps/mustach/mustach-jansson.h"
#include "auth.h"

static ServerContext *serverCtx = NULL;

// Custom partial handler that uses findPartial
static int customPartial(const char *name, struct mustach_sbuf *sbuf) {
    PartialNode *partial = findPartial(name);
    if (!partial || !partial->template) {
        return MUSTACH_ERROR_PARTIAL_NOT_FOUND;
    }
    
    // Get the template content
    sbuf->value = partial->template->content;
    sbuf->freecb = NULL; // Content is managed by the arena
    return MUSTACH_OK;
}

void initMustache(ServerContext *_serverCtx) {
    serverCtx = _serverCtx;
    
    // Set up our custom partial handler
    mustach_wrap_get_partial = customPartial;
}

char* generateTemplateContent(Arena *arena, const TemplateNode *template, int indent) {
    if (!arena || !template) return NULL;
    
    StringBuilder *sb = StringBuilder_new(arena);
    if (!sb) return NULL;
    
    char indentStr[32];
    memset(indentStr, ' ', (size_t)(indent * 2));
    indentStr[indent * 2] = '\0';
    
    StringBuilder_append(sb, "%s%s", indentStr, template->content ? template->content : "");
    
    return arenaDupString(arena, StringBuilder_get(sb));
}

char *generateFullPage(Arena *arena,
                      PageNode *page,
                      LayoutNode *layout,
                      json_t *pipelineResult) {                             
    StringBuilder *sb = StringBuilder_new(arena);

    // Determine which template to use based on pipeline result
    TemplateNode *contentTemplate = NULL;
    
    // Check for error/errors in pipeline result
    json_t *error = json_object_get(pipelineResult, "error");
    json_t *errors = json_object_get(pipelineResult, "errors");
    if ((error || errors) && page->errorBlock) {
        contentTemplate = page->errorBlock->template;
    } else if (!error && !errors && page->successBlock) {
        contentTemplate = page->successBlock->template;
    } else {
        // Fallback to page template if no specific block matches
        contentTemplate = page->template;
    }

    // Generate layout and page content
    if (!layout) {
        // Just use the content template directly
        char *content = generateTemplateContent(arena, contentTemplate, 0);
        if (content) {
            StringBuilder_append(sb, "%s", content);
        }
    } else {
        // Generate layout and content
        char *layoutHtml = generateTemplateContent(arena, layout->bodyTemplate, 0);
        char *pageContent = generateTemplateContent(arena, contentTemplate, 0);

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
    }

    // Render the template with mustache
    char *result = NULL;
    size_t result_size = 0;
    int rc = mustach_jansson_mem(template, strlen(template), data,
                                Mustach_With_AllExtensions, &result, &result_size);

    // Clean up JSON data if we created it
    if (!pipelineResult) {
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

static char* createAnonymousSessionCookie(struct MHD_Connection *connection, ServerContext *ctx, Arena *arena) {
    // Check for existing anonymous session
    const char *existing_token = MHD_lookup_connection_value(
        connection, 
        MHD_COOKIE_KIND, 
        "anonymous_session"
    );

    if (existing_token) {
        return NULL;  // Already has anonymous session
    }

    // Generate new token and store in database
    char *token = generateToken(arena);
    if (!token) {
        return NULL;
    }

    const char *values[] = {token};
    PGresult *result = executeParameterizedQuery(
        ctx->db,
        "INSERT INTO anonymous_sessions (token) VALUES ($1)",
        values, 
        1
    );

    if (!result) {
        return NULL;
    }
    PQclear(result);

    // Create cookie string
    size_t base_cookie_len = strlen("anonymous_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
    size_t token_len = token ? strlen(token) : 0;
    size_t needed_size = base_cookie_len + token_len + 1;

    if (needed_size > 512) {
        // Handle token too large for cookie buffer
        fprintf(stderr, "Token too large for anonymous session cookie buffer\n");
        return NULL;
    }

    char *cookie = arenaAlloc(arena, needed_size);
    if (!cookie) {
        return NULL;
    }

    snprintf(cookie, needed_size, 
            "anonymous_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400",
            token);
    
    return cookie;
}

enum MHD_Result handlePageRequest(struct MHD_Connection *connection,
                                  PageNode *page, Arena *arena,
                                  json_t *pipelineResult) {
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

    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(html), html, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "text/html");

    // Check isLoggedIn from pipelineResult
    json_t *isLoggedIn = json_object_get(pipelineResult, "isLoggedIn");
    if (!json_is_true(isLoggedIn)) {
        char *cookie = createAnonymousSessionCookie(connection, serverCtx, arena);
        if (cookie) {
            MHD_add_response_header(response, "Set-Cookie", cookie);
        }
    }

    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}
