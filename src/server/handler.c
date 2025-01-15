#include "handler.h"
#include "routing.h"
#include "api.h"
#include "css.h"
#include "mustache.h"
#include "pipeline_executor.h"
#include "../arena.h"
#include <string.h>
#include <stdlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
// Add thread-local storage definition
_Thread_local Arena* currentJsonArena = NULL;

// Add JSON memory management functions
static void* jsonArenaMalloc(size_t size) {
    if (!currentJsonArena) return NULL;
    return arenaAlloc(currentJsonArena, size);
}

static void jsonArenaFree(void *ptr) {
    // No-op since we're using arena allocation
    (void)ptr;
}

void initRequestJsonArena(Arena *arena) {
    currentJsonArena = arena;
    json_set_alloc_funcs(jsonArenaMalloc, jsonArenaFree);
}

void cleanupRequestJsonArena(void) {
    currentJsonArena = NULL;
}

static enum MHD_Result post_iterator(void *cls,
                                   enum MHD_ValueKind kind,
                                   const char *key,
                                   const char *filename,
                                   const char *content_type,
                                   const char *transfer_encoding,
                                   const char *data,
                                   uint64_t off,
                                   size_t size) {
    (void)kind; (void)filename; (void)content_type;
    (void)transfer_encoding; (void)off; (void)size;
    
    struct PostData *post_data = cls;
    
    // Store the value in our array using arena allocation
    if (post_data->value_count < 32) {
        post_data->values[post_data->value_count] = arenaDupString(post_data->arena, data);
        post_data->keys[post_data->value_count] = arenaDupString(post_data->arena, key);
        post_data->value_count++;
    }
    
    return MHD_YES;
}

enum MHD_Result handleRequest(ServerContext *ctx,
                            struct MHD_Connection *connection,
                            const char *url,
                            const char *method,
                            const char *version,
                            const char *upload_data,
                            size_t *upload_data_size,
                            void **con_cls) {
    (void)version; (void)ctx;

    // First call for this connection
    if (*con_cls == NULL) {
        Arena *arena = createArena(1024 * 1024); // 1MB initial size
        
        if (strcmp(method, "POST") == 0) {
            struct PostContext *post = arenaAlloc(arena, sizeof(struct PostContext));
            post->data = NULL;
            post->size = 0;
            post->processed = 0;
            post->raw_json = NULL;
            post->pp = NULL;  // Initialize pp to NULL for all requests
            post->post_data.connection = connection;
            post->post_data.error = 0;
            post->post_data.value_count = 0;
            post->post_data.arena = arena;
            post->arena = arena;
            
            // Check content type for JSON
            const char *content_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
            if (content_type && strstr(content_type, "application/json") != NULL) {
                post->type = REQUEST_TYPE_JSON_POST;
            } else {
                post->type = REQUEST_TYPE_POST;
                post->pp = MHD_create_post_processor(connection,
                                                   32 * 1024,  // 32k buffer
                                                   post_iterator,
                                                   &post->post_data);
                if (!post->pp) {
                    freeArena(arena);
                    return MHD_NO;
                }
            }
            *con_cls = post;
            return MHD_YES;
        }
        // For GET requests, create a simple context with an arena
        struct RequestContext *reqctx = arenaAlloc(arena, sizeof(struct RequestContext));
        reqctx->arena = arena;
        reqctx->type = REQUEST_TYPE_GET;
        *con_cls = reqctx;
        return MHD_YES;
    }

    // Get the arena from the context
    Arena *requestArena;
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post = *con_cls;
        requestArena = post->arena;
    } else {
        struct RequestContext *reqctx = *con_cls;
        requestArena = reqctx->arena;
    }

    // Initialize JSON arena for this request
    initRequestJsonArena(requestArena);

    // Handle POST data
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post = *con_cls;
        
        if (*upload_data_size != 0) {
            if (post->type == REQUEST_TYPE_JSON_POST) {
                // Accumulate JSON data using arena allocation
                if (!post->raw_json) {
                    post->raw_json = arenaAlloc(post->arena, *upload_data_size + 1);
                    memcpy(post->raw_json, upload_data, *upload_data_size);
                    post->size = *upload_data_size;
                } else {
                    printf("post\n");
                    char *new_buffer = arenaAlloc(post->arena, post->size + *upload_data_size + 1);
                    memcpy(new_buffer, post->raw_json, post->size);
                    memcpy(new_buffer + post->size, upload_data, *upload_data_size);
                    post->raw_json = new_buffer;
                    post->size += *upload_data_size;
                }
                post->raw_json[post->size] = '\0';
            } else {
                if (MHD_post_process(post->pp, upload_data, *upload_data_size) == MHD_NO) {
                    return MHD_NO;
                }
            }
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        if (post->post_data.error) {
            return MHD_NO;
        }
    }

    // Check for CSS endpoint first since it's a special case
    if (strcmp(url, "/styles.css") == 0) {
        return handleCssRequest(connection, requestArena);
    }

    // Find route using unified routing
    RouteMatch match = findRoute(url, method, requestArena);
    
    // Build request context once - AFTER all POST data is processed
    json_t *requestContext = buildRequestContextJson(
        connection, requestArena, *con_cls, 
        method, url, version, &match.params
    );
    
    // Execute pipeline if exists
    json_t *pipelineResult = NULL;
    PipelineStepNode *pipeline = NULL;
    
    // Get pipeline from either API or page
    if (match.type == ROUTE_TYPE_API && match.endpoint.api->uses_pipeline) {
        pipeline = match.endpoint.api->pipeline;
    } else if (match.type == ROUTE_TYPE_PAGE && match.endpoint.page->pipeline) {
        pipeline = match.endpoint.page->pipeline;
    }
    
    // Execute pipeline once if it exists
    if (pipeline) {
        pipelineResult = executePipeline(ctx, pipeline, requestContext, requestArena);
    }
    
    json_decref(requestContext);

    // enum MHD_Result handleApiRequest(
    //     struct MHD_Connection * connection, ApiEndpoint * api,
    //     const char *method, void *con_cls, Arena *arena, json_t *pipelineResult)

        // Handle based on route type
        switch (match.type) {
    case ROUTE_TYPE_API:
      return handleApiRequest(connection, match.endpoint.api, method,
                               *con_cls, requestArena, pipelineResult);

    case ROUTE_TYPE_PAGE:
      return handleMustachePageRequest(connection, url, requestArena,
                                       pipelineResult);

    case ROUTE_TYPE_NONE:
      return MHD_NO;
    }

    return MHD_NO;
}

void handleRequestCompleted(ServerContext *ctx,
                          struct MHD_Connection *connection,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe) {
    cleanupRequestJsonArena();
    
    (void)ctx; (void)connection; (void)toe;
    
    if (*con_cls != NULL) {
        struct RequestContext *reqctx = *con_cls;
        if (!reqctx) {
            return;
        }
        
        if (reqctx->type == REQUEST_TYPE_POST || reqctx->type == REQUEST_TYPE_JSON_POST) {
            struct PostContext *post = (struct PostContext *)reqctx;
            if (post->pp) {
                MHD_destroy_post_processor(post->pp);
            }
            // No need to free raw_json, data, values, or keys - they're all in the arena
            freeArena(post->arena);
        } else {
            freeArena(reqctx->arena);
        }
        *con_cls = NULL;
    }
}

