#include "handler.h"
#include "routing.h"
#include "auth.h"
#include "github.h"
#include "api.h"
#include "css.h"
#include "mustache.h"
#include "pipeline_executor.h"
#include "validation.h"
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

static struct PostContext* initializePostContext(Arena *arena, struct MHD_Connection *connection) {
    struct PostContext *post = arenaAlloc(arena, sizeof(struct PostContext));
    post->data = NULL;
    post->size = 0;
    post->processed = 0;
    post->raw_json = NULL;
    post->pp = NULL;
    post->post_data.connection = connection;
    post->post_data.error = 0;
    post->post_data.value_count = 0;
    post->post_data.arena = arena;
    post->arena = arena;
    return post;
}

static enum MHD_Result setupPostProcessor(struct PostContext *post, struct MHD_Connection *connection, Arena *arena) {
    const char *content_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
    
    if (content_type && strstr(content_type, "application/json") != NULL) {
        post->type = REQUEST_TYPE_JSON_POST;
    } else {
        post->type = REQUEST_TYPE_POST;
        post->pp = MHD_create_post_processor(connection, 32 * 1024, post_iterator, &post->post_data);
        if (!post->pp) {
            freeArena(arena);
            return MHD_NO;
        }
    }
    
    return MHD_YES;
}

static struct RequestContext* initializeGetContext(Arena *arena) {
    struct RequestContext *reqctx = arenaAlloc(arena, sizeof(struct RequestContext));
    reqctx->arena = arena;
    reqctx->type = REQUEST_TYPE_GET;
    return reqctx;
}

static enum MHD_Result handleJsonPostData(struct PostContext *post, const char *upload_data, size_t *upload_data_size) {
    // Accumulate JSON data using arena allocation
    if (!post->raw_json) {
        post->raw_json = arenaAlloc(post->arena, *upload_data_size + 1);
        memcpy(post->raw_json, upload_data, *upload_data_size);
        post->size = *upload_data_size;
    } else {
        char *new_buffer = arenaAlloc(post->arena, post->size + *upload_data_size + 1);
        memcpy(new_buffer, post->raw_json, post->size);
        memcpy(new_buffer + post->size, upload_data, *upload_data_size);
        post->raw_json = new_buffer;
        post->size += *upload_data_size;
    }
    post->raw_json[post->size] = '\0';
    return MHD_YES;
}

// Helper callback for MHD_get_connection_values
static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
  (void)kind; // Suppress unused parameter warning
  json_t *obj = (json_t *)cls;
  json_object_set_new(obj, key, json_string(value));
  return MHD_YES;
}

static json_t* buildRequestContextJson(ServerContext *ctx, struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version,
                                   RouteParams *params) {
    (void)arena;
    json_t *context = json_object();

    // Add method, url and version to context
    json_object_set_new(context, "method", json_string(method));
    json_object_set_new(context, "url", json_string(url));
    json_object_set_new(context, "version", json_string(version));
    
    // Add user to context if logged in
    json_t *user = getUser(ctx, connection);
    if (user) {
        json_object_set_new(context, "user", user);
        json_object_set_new(context, "isLoggedIn", json_true());
    } else {
        json_object_set_new(context, "isLoggedIn", json_false());
    }
    
    // Build query parameters object
    json_t *query = json_object();
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND,
        jsonKvIterator, query);
    json_object_set_new(context, "query", query);
    
    // Build headers object
    json_t *headers = json_object();
    MHD_get_connection_values(connection, MHD_HEADER_KIND,
        jsonKvIterator, headers);
    json_object_set_new(context, "headers", headers);
    
    // Build cookies object
    json_t *cookies = json_object();
    MHD_get_connection_values(connection, MHD_COOKIE_KIND,
        jsonKvIterator, cookies);
    json_object_set_new(context, "cookies", cookies);

    // Add params to context
    json_t *params_obj = json_object();
    for (int i = 0; i < params->count; i++) {
        json_object_set_new(params_obj, params->params[i].name, json_string(params->params[i].value));
    }
    json_object_set_new(context, "params", params_obj);

    // Build body object
    json_t *body = json_object();
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post_ctx = con_cls;
        if (post_ctx) {
            if (post_ctx->type == REQUEST_TYPE_JSON_POST && post_ctx->raw_json) {
                // Parse JSON data
                json_error_t error;
                json_t *json_body = json_loads(post_ctx->raw_json, 0, &error);
                if (json_body) {
                    // Replace the empty body object with the parsed JSON
                    json_decref(body);
                    body = json_body;
                }
            } else if (post_ctx->type == REQUEST_TYPE_POST) {
                // Handle form data as before
                for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
                    const char *value = post_ctx->post_data.values[i];
                    const char *key = post_ctx->post_data.keys[i];
                    if (value) {
                        json_object_set_new(body, key, json_string(value));
                    }
                }
            }
        }
    }
    json_object_set_new(context, "body", body);

    return context;
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

    bool isPost = strcmp(method, "POST") == 0;

    // First call for this connection
    if (*con_cls == NULL) {
        Arena *arena = createArena(1024 * 1024); // 1MB initial size
        
        if (isPost) {
            struct PostContext *post = initializePostContext(arena, connection);
            enum MHD_Result result = setupPostProcessor(post, connection, arena);
            if (result != MHD_YES) {
                return result;
            }
            *con_cls = post;
            return MHD_YES;
        }
        
        *con_cls = initializeGetContext(arena);
        return MHD_YES;
    }

    // Get the arena from the context
    Arena *requestArena;
    if (isPost) {
        struct PostContext *post = *con_cls;
        requestArena = post->arena;
    } else {
        struct RequestContext *reqctx = *con_cls;
        requestArena = reqctx->arena;
    }

    // Initialize JSON arena for this request
    initRequestJsonArena(requestArena);

    // Handle POST data
    if (isPost) {
        struct PostContext *post = *con_cls;
        
        if (*upload_data_size != 0) {
            if (post->type == REQUEST_TYPE_JSON_POST) {
                if (handleJsonPostData(post, upload_data, upload_data_size) != MHD_YES) {
                    return MHD_NO;
                }
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
        
        // Handle login endpoint after all POST data is processed
        if (strcmp(url, "/login") == 0) {
            return handleLoginRequest(ctx, connection, post);
        }
        
        // Handle logout endpoint
        if (strcmp(url, "/logout") == 0) {
            return handleLogoutRequest(ctx, connection);
        }
        
        // Handle register endpoint
        if (strcmp(url, "/register") == 0) {
            return handleRegisterRequest(ctx, connection, post);
        }

        // Handle resend verification endpoint
        if (strcmp(url, "/resend-verification") == 0) {
            return handleResendVerificationRequest(ctx, connection);
        }

        // Handle forgot password endpoint
        if (strcmp(url, "/forgot-password") == 0) {
            return handleForgotPasswordRequest(ctx, connection, post);
        }

        // Handle reset password endpoint
        if (strcmp(url, "/reset-password") == 0) {
            return handleResetPasswordRequest(ctx, connection, post);
        }
    } else {
        // Handle verify email endpoint (GET request)
        if (strcmp(url, "/verify-email") == 0) {
            const char *token = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "token");
            if (token) {
                return handleVerifyEmailRequest(ctx, connection, token);
            }
        }
        // Handle GitHub OAuth endpoints
        if (strcmp(url, "/auth/github") == 0) {
            return handleGithubAuthRequest(ctx, connection);
        }
        if (strcmp(url, "/auth/github/callback") == 0) {
            return handleGithubCallback(ctx, connection, NULL);
        }
    }

    // Check for CSS endpoint first since it's a special case
    if (strcmp(url, "/styles.css") == 0) {
        return handleCssRequest(connection, requestArena);
    }

    // Find route using unified routing
    RouteMatch match = findRoute(url, method, requestArena);

    // Build request context once - AFTER all POST data is processed
    json_t *requestContext =
        buildRequestContextJson(ctx, connection, requestArena, *con_cls, method, url,
                                version, &match.params);

    // Early validation for POST requests
    if (isPost) {
        struct PostContext *post = *con_cls;
        json_t *validation_errors = NULL;
        
        if (match.type == ROUTE_TYPE_API && match.endpoint.api->apiFields) {
            if (post->type == REQUEST_TYPE_JSON_POST) {
                validation_errors = validateJsonFields(requestArena, match.endpoint.api->apiFields, post);
                if (validation_errors) {
                    char *error_str = json_dumps(validation_errors, 0);
                    struct MHD_Response *response =
                        MHD_create_response_from_buffer(strlen(error_str),
                                                      error_str,
                                                      MHD_RESPMEM_PERSISTENT);
                    MHD_add_response_header(response, "Content-Type", "application/json");
                    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
                    MHD_destroy_response(response);
                    return ret;
                }
            }
        } else if (match.type == ROUTE_TYPE_PAGE && match.endpoint.page->fields) {
            if (post->type == REQUEST_TYPE_POST) {
                // Execute reference data before validation if it exists
                if (match.endpoint.page->referenceData) {
                    json_t *refData = executePipeline(ctx, match.endpoint.page->referenceData, requestContext, requestArena);
                    if (refData) {
                        json_object_update(requestContext, refData);
                    }
                }
                
                validation_errors = validateFormFields(requestArena, match.endpoint.page->fields, post);
                if (validation_errors) {
                    json_object_update(requestContext, validation_errors);
                    return handlePageRequest(connection, match.endpoint.page,
                                             requestArena, requestContext);
                }
            }
        }
    }
    
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
    } else {
        json_t *context = json_object();
        json_object_set_new(context, "request", json_deep_copy(requestContext));
        pipelineResult = context;
    }

    

    // Handle based on route type
    switch (match.type) {
        case ROUTE_TYPE_API:
            return handleApiRequest(connection, match.endpoint.api, method, pipelineResult);

        case ROUTE_TYPE_PAGE:
          // add requestContext to pipelineResult
          json_object_set_new(pipelineResult, "request", json_deep_copy(requestContext));
          return handlePageRequest(connection, match.endpoint.page, requestArena,
                                   pipelineResult);

        case ROUTE_TYPE_NONE:
            break;
    }

    char *not_found = "<html><body><h1>404 Not Found</h1></body></html>";
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(not_found), not_found, MHD_RESPMEM_PERSISTENT);
    enum MHD_Result ret =
        MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);

    return ret;
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
