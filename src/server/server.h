#ifndef SERVER_H
#define SERVER_H

#include "../ast.h"
#include "../arena.h"
#include "db.h"

// Server context structure to hold server-wide state
typedef struct ServerContext {
    WebsiteNode *website;
    Arena *arena;
    Database *db;
    struct MHD_Daemon *daemon;
} ServerContext;

// Server operations
ServerContext* startServer(WebsiteNode *website, Arena *arena);
void stopServer(void);

#endif // SERVER_H
