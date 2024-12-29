#ifndef SERVER_API_H
#define SERVER_API_H

#include <microhttpd.h>
#include "../ast.h"
#include "../arena.h"

// API request handling
enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               void *con_cls);

// API response generation
char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls);

#endif // SERVER_API_H
