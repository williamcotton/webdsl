#ifndef SERVER_API_H
#define SERVER_API_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"
#include <jq.h>
#include <pthread.h>

// API request handling
enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               void *con_cls,
                               Arena *arena);

// API response generation
char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls);

// Internal functions (if needed by tests)
#ifdef TESTING
static char* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena);
#endif

#endif // SERVER_API_H
