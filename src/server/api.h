#ifndef SERVER_API_H
#define SERVER_API_H

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
                                 ApiEndpoint *api, const char *method,
                                 json_t *pipelineResult);

#endif // SERVER_API_H
