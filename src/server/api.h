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
#include "server.h"
#include "route_params.h"

// API request handling
enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               const char *url,
                               const char *version,
                               void *con_cls,
                               Arena *arena,
                               ServerContext *ctx,
                               RouteParams *params);

// API response generation
json_t* generateApiResponse(Arena *arena, 
                        ApiEndpoint *endpoint, 
                        void *con_cls,
                        json_t *requestContext,
                        ServerContext *ctx);

// Pipeline step executor setup
void setupStepExecutor(PipelineStepNode *step);

json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena,
                                void *con_cls, const char *method,
                                const char *url, const char *version,
                                RouteParams *params);

#endif // SERVER_API_H
