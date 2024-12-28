#ifndef SERVER_H
#define SERVER_H

#include "arena.h"
#include "ast.h"

// Server operations
void startServer(WebsiteNode *website, Arena *arena);
void stopServer(void);
char* generateHtmlContent(Arena *arena, const ContentNode *cn, int indent);
char *generateCss(Arena *arena, StyleBlockNode *styleHead);

#endif // SERVER_H
