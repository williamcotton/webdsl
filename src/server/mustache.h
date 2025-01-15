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
char *generateFullMustachePage(Arena *arena,
                               PageNode *page,
                               LayoutNode *layout,
                               json_t *pipelineResult);

// Request handler for mustache pages
enum MHD_Result handleMustachePageRequest(struct MHD_Connection *connection,
                                          const char *url, Arena *arena,
                                          json_t *pipelineResult);

#endif // SERVER_MUSTACHE_H
