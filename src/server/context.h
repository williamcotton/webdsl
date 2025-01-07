#ifndef SERVER_CONTEXT_H
#define SERVER_CONTEXT_H

#include "../ast.h"
#include "../arena.h"
#include "db.h"
#include <microhttpd.h>

// Server context structure to hold server-wide state
typedef struct ServerContext {
    WebsiteNode *website;
    Arena *arena;
    Database *db;
    struct MHD_Daemon *daemon;
} ServerContext;

#endif // SERVER_CONTEXT_H
