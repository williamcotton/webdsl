#ifndef SERVER_HTML_H
#define SERVER_HTML_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"
#include "server.h"

// Initialize HTML subsystem
void initCss(ServerContext *ctx);

// HTML generation
char* generateCss(Arena *arena, StyleBlockNode *styleHead);

enum MHD_Result handleCssRequest(struct MHD_Connection *connection, Arena *arena);

#endif // SERVER_HTML_H
