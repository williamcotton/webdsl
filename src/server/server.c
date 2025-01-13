#include "server.h"
#include "handler.h"
#include "routing.h"
#include "db.h"
#include "lua.h"
#include "css.h"
#include "mustache.h"
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <jq.h>
#include <pthread.h>

static ServerContext *serverCtx = NULL;

static enum MHD_Result handler_adapter(void *cls,
                                struct MHD_Connection *connection,
                                const char *url,
                                const char *method,
                                const char *version,
                                const char *upload_data,
                                size_t *upload_data_size,
                                void **con_cls) {
    return handleRequest((ServerContext *)cls, connection, url, method, version,
                        upload_data, upload_data_size, con_cls);
}

ServerContext* startServer(WebsiteNode *website, Arena *arena) {
    // Create server context
    serverCtx = arenaAlloc(arena, sizeof(ServerContext));
    serverCtx->website = website;
    serverCtx->arena = arena;
    
    buildRouteMaps(website, arena);

    // Initialize database connection
    if (website->databaseUrl) {
        serverCtx->db = initDatabase(arena, website->databaseUrl);
    }
    if (!serverCtx->db) {
        fprintf(stderr, "Failed to connect to database: %s\n",
                website->databaseUrl ? website->databaseUrl
                                   : "no database URL configured");
        exit(1);
    }

    // Initialize subsystems
    initDb(serverCtx);
    initCss(serverCtx);
    initMustache(serverCtx);

    // Initialize Lua subsystem
    if (!initLua(serverCtx)) {
        fprintf(stderr, "Failed to initialize Lua subsystem\n");
        exit(1);
    }

    // Get port number from website definition, default to 8080 if not specified
    uint16_t port = website->port > 0 ? (uint16_t)website->port : 8080;

    serverCtx->daemon = MHD_start_daemon(MHD_USE_POLL_INTERNAL_THREAD | MHD_USE_INTERNAL_POLLING_THREAD, port,
                            NULL, NULL, 
                            handler_adapter, serverCtx,  // Pass ctx as user data
                            MHD_OPTION_CONNECTION_TIMEOUT, 30,
                            MHD_OPTION_THREAD_POOL_SIZE, 8,
                            MHD_OPTION_NOTIFY_COMPLETED, handleRequestCompleted, serverCtx,  // Pass ctx to completion handler
                            MHD_OPTION_END);
    
    if (serverCtx->daemon == NULL) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        exit(1);
    }

    printf("Server started on port %d\n", port);
    
    return serverCtx;
}

void stopServer(void) {
    if (!serverCtx) {
        return;
    }

    if (serverCtx->daemon) {
        MHD_stop_daemon(serverCtx->daemon);
    }

    cleanupJQCache();
    cleanupLua();

    if (serverCtx->db) {
        closeDatabase(serverCtx->db);
    }

    serverCtx = NULL;
}
