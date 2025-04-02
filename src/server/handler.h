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
    REQUEST_TYPE_PUT,
    REQUEST_TYPE_PATCH,
    REQUEST_TYPE_DELETE,
    REQUEST_TYPE_JSON_POST,
    REQUEST_TYPE_JSON_PUT,
    REQUEST_TYPE_JSON_PATCH,
    REQUEST_TYPE_MULTIPART
};

// File upload structure
struct FileUpload {
    char *fieldname;     // Form field name
    char *filename;      // Original filename
    char *mimetype;      // Content type
    char *tempPath;      // Path to temporary file
    size_t max_size;     // Maximum allowed file size
    size_t size;        // File size
    FILE *fp;    // Padding
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
    struct FileUpload *files;  // Array of file uploads
    size_t file_count;         // Number of files
    size_t file_capacity;      // Capacity of files array
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
