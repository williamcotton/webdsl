#ifndef SERVER_MUSTACHE_H
#define SERVER_MUSTACHE_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"
#include "server.h"

// Initialize mustache subsystem
void initMustache(ServerContext *ctx);

// Generate mustache content from a ContentNode
char* generateMustacheContent(Arena *arena, const ContentNode *cn, int indent);

// Generate full page with mustache templates
char *generateFullMustachePage(struct MHD_Connection *connection,
                               ApiEndpoint *api, const char *method,
                               const char *url, const char *version,
                               void *con_cls, Arena *arena,
                               ServerContext *ctx, PageNode *page,
                               LayoutNode *layout);

// Request handler for mustache pages
enum MHD_Result handleMustachePageRequest(struct MHD_Connection *connection,
                                          ApiEndpoint *api, const char *method,
                                          const char *url, const char *version,
                                          void *con_cls, Arena *arena,
                                          ServerContext *ctx);

#endif // SERVER_MUSTACHE_H
