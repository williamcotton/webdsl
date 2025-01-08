#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include <microhttpd.h>
#include "../ast.h"
#include "db.h"

typedef struct ServerContext {
    WebsiteNode *website;
    Database *db;
    struct MHD_Daemon *daemon;
    Arena *arena;
} ServerContext;

ServerContext* startServer(WebsiteNode *website, Arena *arena);
void stopServer(void);

#endif // SERVER_SERVER_H
