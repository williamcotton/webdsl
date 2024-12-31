#include "server.h"
#include "handler.h"
#include "routing.h"
#include "../db.h"
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <jq.h>
#include <pthread.h>

WebsiteNode *currentWebsite = NULL;
struct MHD_Daemon *httpd = NULL;
Arena *serverArena = NULL;
Database *db = NULL;

void startServer(WebsiteNode *website, Arena *arena) {
    currentWebsite = website;
    serverArena = arena;
    buildRouteMaps(website, serverArena);

    // Initialize database connection
    if (website->databaseUrl) {
        db = initDatabase(serverArena, website->databaseUrl);
    }
    if (!db) {
        fprintf(stderr, "Failed to connect to database: %s\n", 
               website->databaseUrl ? website->databaseUrl : "no database URL configured");
        exit(1);
    }

    // Get port number from website definition, default to 8080 if not specified
    uint16_t port = website->port > 0 ? (uint16_t)website->port : 8080;

    httpd = MHD_start_daemon(MHD_USE_POLL_INTERNAL_THREAD | MHD_USE_INTERNAL_POLLING_THREAD, port,
                            NULL, NULL, &handleRequest, NULL,
                            MHD_OPTION_CONNECTION_TIMEOUT, 30,
                            MHD_OPTION_THREAD_POOL_SIZE, 4,
                            MHD_OPTION_NOTIFY_COMPLETED, handleRequestCompleted, NULL,
                            MHD_OPTION_END);
    if (httpd == NULL) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        exit(1);
    }

    printf("Server started on port %d\n", port);
}

void stopServer(void) {
    if (httpd) {
        MHD_stop_daemon(httpd);
        httpd = NULL;
    }

    cleanupJQCache();

    if (currentWebsite) {
        currentWebsite = NULL;
    }

    if (db) {
        closeDatabase(db);
        db = NULL;
    }
}
