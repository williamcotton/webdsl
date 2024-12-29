#ifndef SERVER_HANDLER_H
#define SERVER_HANDLER_H

#include <microhttpd.h>

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

#endif // SERVER_HANDLER_H
