#include "css.h"
#include "../stringbuilder.h"
#include <string.h>

static ServerContext *ctx = NULL;

void initCss(ServerContext *serverCtx) {
    ctx = serverCtx;
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

enum MHD_Result handleCssRequest(struct MHD_Connection *connection, Arena *arena) {
    char *css = generateCss(arena, ctx->website->styleHead);
    char *css_copy = strdup(css);
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(css_copy), css_copy,
                                               MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "text/css");
    return MHD_queue_response(connection, MHD_HTTP_OK, response);
}
