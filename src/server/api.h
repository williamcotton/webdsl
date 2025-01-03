#ifndef SERVER_API_H
#define SERVER_API_H

#include "../arena.h"
#include "../ast.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include <jq.h>
#include <microhttpd.h>
#include <pthread.h>

// API request handling
enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               const char *url,
                               const char *version,
                               void *con_cls,
                               Arena *arena);

// API response generation
char* generateApiResponse(Arena *arena, 
                        ApiEndpoint *endpoint, 
                        void *con_cls,
                        json_t *requestContext);

// Internal functions (if needed by tests)
#ifdef TESTING
char* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena);
#endif

#endif // SERVER_API_H
