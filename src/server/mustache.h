#ifndef SERVER_MUSTACHE_H
#define SERVER_MUSTACHE_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"
#include "server.h"

// Initialize mustache subsystem
void initMustache(ServerContext *ctx);

// Generate content from a template node
char* generateTemplateContent(Arena *arena, const TemplateNode *template, int indent);

// Generate full page with templates
char *generateFullPage(Arena *arena,
                      PageNode *page,
                      LayoutNode *layout,
                      json_t *pipelineResult);

// Request handler for mustache pages
enum MHD_Result handleMustachePageRequest(struct MHD_Connection *connection,
                                        const char *url, Arena *arena,
                                        json_t *pipelineResult);

#endif // SERVER_MUSTACHE_H
