#ifndef SERVER_HTML_H
#define SERVER_HTML_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"
#include "server.h"

// Initialize HTML subsystem
void initHtml(ServerContext *ctx);

// HTML generation
char* generateHtmlContent(Arena *arena, const ContentNode *cn, int indent);
char* generateFullHtml(Arena *arena, PageNode *page, LayoutNode *layout);
char* generateCss(Arena *arena, StyleBlockNode *styleHead);

// Request handlers
enum MHD_Result handlePageRequest(struct MHD_Connection *connection, const char *url, Arena *arena);
enum MHD_Result handleCssRequest(struct MHD_Connection *connection, Arena *arena);

#endif // SERVER_HTML_H
