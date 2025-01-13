#ifndef SERVER_HANDLER_H
#define SERVER_HANDLER_H

#include <microhttpd.h>
#include "../arena.h"
#include "server.h"

// Add thread-local storage for JSON arena
extern _Thread_local Arena* currentJsonArena;

enum RequestType {
    REQUEST_TYPE_GET,
    REQUEST_TYPE_POST,
    REQUEST_TYPE_JSON_POST
};

struct PostData {
    struct MHD_Connection *connection;
    char *values[32];
    char *keys[32];
    size_t value_count;
    int error;
    uint32_t : 32;
    Arena *arena;  // Add arena field for form data allocation
};

struct PostContext {
    enum RequestType type;
    uint32_t : 32;
    struct MHD_PostProcessor *pp;
    char *data;
    char *raw_json;
    size_t size;
    size_t processed;
    struct PostData post_data;
    Arena *arena;
};

struct RequestContext {
    enum RequestType type;
    uint32_t : 32;
    Arena *arena;
};

// Request handling
enum MHD_Result handleRequest(ServerContext *ctx,
                            struct MHD_Connection *connection,
                            const char *url,
                            const char *method,
                            const char *version,
                            const char *upload_data,
                            size_t *upload_data_size,
                            void **con_cls);

// Request cleanup
void handleRequestCompleted(ServerContext *ctx,
                          struct MHD_Connection *connection,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe);

// Add JSON memory management functions
void initRequestJsonArena(Arena *arena);
void cleanupRequestJsonArena(void);

#endif // SERVER_HANDLER_H
