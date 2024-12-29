#ifndef SERVER_H
#define SERVER_H

#include "../ast.h"
#include "../arena.h"
#include "../db.h"

// Global state accessible to other server components
extern WebsiteNode *currentWebsite;
extern Arena *serverArena;
extern Database *db;
extern struct MHD_Daemon *httpd;

// Server operations
void startServer(WebsiteNode *website, Arena *arena);
void stopServer(void);

#endif // SERVER_H
