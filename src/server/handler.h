#ifndef SERVER_HANDLER_H
#define SERVER_HANDLER_H

#include <microhttpd.h>
#include "../arena.h"

// Add thread-local storage for JSON arena
extern _Thread_local Arena* currentJsonArena;

enum RequestType {
    REQUEST_TYPE_GET,
    REQUEST_TYPE_POST
};

// Forward declarations
struct PostData {
    char *values[32];  // Array of values matching the expected fields
    struct MHD_Connection *connection;
    size_t value_count;
    int error;
    uint32_t : 32;
};

struct PostContext {
    char *data;
    size_t size;
    size_t processed;
    struct MHD_PostProcessor *pp;
    struct PostData post_data;
    Arena *arena;
    enum RequestType type;
    uint32_t : 32;
};

struct RequestContext {
    Arena *arena;
    enum RequestType type;
    uint32_t : 32;
};

// Request handling
enum MHD_Result handleRequest(void *cls,
                            struct MHD_Connection *connection,
                            const char *url,
                            const char *method,
                            const char *version,
                            const char *upload_data,
                            size_t *upload_data_size,
                            void **con_cls);

// Request cleanup
void handleRequestCompleted(void *cls,
                          struct MHD_Connection *connection,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe);

// Add JSON memory management functions
void initRequestJsonArena(Arena *arena);
void cleanupRequestJsonArena(void);

#endif // SERVER_HANDLER_H
