#include "server.h"
#include "db.h"
#include "css.h"
#include "lua.h"
#include "mustache.h"
#include "email.h"
#include "routing.h"
#include "handler.h"
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <libpq-fe.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <jq.h>

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
    if (website->databaseUrl.type != VALUE_NULL) {
        char *resolvedUrl = resolveString(arena, &website->databaseUrl);
        if (resolvedUrl) {
            serverCtx->db = initDatabase(arena, resolvedUrl);
            if (!serverCtx->db) {
                fprintf(stderr, "Failed to initialize database\n");
                return NULL;
            }
        } else {
            fprintf(stderr, "Failed to resolve database URL\n");
            exit(1);
        }
    } else {
        serverCtx->db = NULL;
        fprintf(stderr, "No database URL configured - running without database\n");
    }

    // Initialize subsystems
    initDb(serverCtx);
    initCss(serverCtx);
    initMustache(serverCtx);
    initEmail(serverCtx);

    // Initialize Lua subsystem
    if (!initLua(serverCtx)) {
        fprintf(stderr, "Failed to initialize Lua subsystem\n");
        exit(1);
    }

    // Get port number from website definition, default to 8080 if not specified
    uint16_t port = 8080;  // Default port
    if (website->port.type != VALUE_NULL) {
        int portNum;
        if (resolveNumber(&website->port, &portNum)) {
            if (portNum > 0 && portNum <= 65535) {
                port = (uint16_t)portNum;
            } else {
                fprintf(stderr, "Invalid port number: %d (must be between 1 and 65535)\n", portNum);
                exit(1);
            }
        } else {
            fprintf(stderr, "Failed to resolve port number\n");
            exit(1);
        }
    }

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
