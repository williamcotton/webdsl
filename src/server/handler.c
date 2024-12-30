#include "handler.h"
#include "routing.h"
#include "api.h"
#include "html.h"
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
    (void)kind; (void)filename; (void)content_type; (void)key;
    (void)transfer_encoding; (void)off; (void)size;
    
    struct PostData *post_data = cls;
    
    // Store the value in our array
    if (post_data->value_count < 32) {
        post_data->values[post_data->value_count++] = strdup(data);
    }
    
    return MHD_YES;
}

enum MHD_Result handleRequest(void *cls,
                            struct MHD_Connection *connection,
                            const char *url,
                            const char *method,
                            const char *version,
                            const char *upload_data,
                            size_t *upload_data_size,
                            void **con_cls) {
    (void)cls; (void)version;

    // First call for this connection
    if (*con_cls == NULL) {
        if (strcmp(method, "POST") == 0) {
            struct PostContext *post = malloc(sizeof(struct PostContext));
            post->data = NULL;
            post->size = 0;
            post->processed = 0;
            post->post_data.connection = connection;
            post->post_data.error = 0;
            post->post_data.value_count = 0;
            post->arena = createArena(1024 * 1024); // 1MB initial size
            
            post->pp = MHD_create_post_processor(connection,
                                               32 * 1024,  // 32k buffer
                                               post_iterator,
                                               &post->post_data);
            if (!post->pp) {
                freeArena(post->arena);
                free(post);
                return MHD_NO;
            }
            post->type = REQUEST_TYPE_POST;
            *con_cls = post;
            return MHD_YES;
        }
        // For GET requests, create a simple context with an arena
        struct RequestContext *ctx = malloc(sizeof(struct RequestContext));
        ctx->arena = createArena(1024 * 1024); // 1MB initial size
        ctx->type = REQUEST_TYPE_GET;
        *con_cls = ctx;
        return MHD_YES;
    }

    // Get the arena from the context
    Arena *requestArena;
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post = *con_cls;
        requestArena = post->arena;
    } else {
        struct RequestContext *ctx = *con_cls;
        requestArena = ctx->arena;
    }

    // Initialize JSON arena for this request
    initRequestJsonArena(requestArena);

    // Handle POST data
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post = *con_cls;
        
        if (*upload_data_size != 0) {
            if (MHD_post_process(post->pp, upload_data, *upload_data_size) == MHD_NO) {
                return MHD_NO;
            }
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        if (post->post_data.error) {
            return MHD_NO;
        }
    }

    // Check for API endpoint first
    ApiEndpoint *api = findApi(url, method);
    if (api) {
        return handleApiRequest(connection, api, method, *con_cls, requestArena);
    }

    // Handle regular pages and CSS
    if (strcmp(method, "GET") != 0) {
        return MHD_NO;
    }

    if (strcmp(url, "/styles.css") == 0) {
        return handleCssRequest(connection, requestArena);
    } 
    
    return handlePageRequest(connection, url, requestArena);
}

void handleRequestCompleted(void *cls,
                          struct MHD_Connection *connection,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe) {
    cleanupRequestJsonArena();
    
    (void)cls; (void)connection; (void)toe;
    
    if (*con_cls != NULL) {
        if (((struct RequestContext*)*con_cls)->type == REQUEST_TYPE_POST) {
            struct PostContext *post = *con_cls;
            if (post) {
                if (post->data)
                    free(post->data);
                if (post->pp)
                    MHD_destroy_post_processor(post->pp);
                for (size_t i = 0; i < post->post_data.value_count; i++) {
                    free(post->post_data.values[i]);
                }
                freeArena(post->arena);
                free(post);
            }
        } else {
            struct RequestContext *ctx = *con_cls;
            freeArena(ctx->arena);
            free(ctx);
        }
        *con_cls = NULL;
    }
}
