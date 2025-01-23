#include "handler.h"
#include "routing.h"
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
#include <argon2.h>
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

static bool isLoggedIn(struct MHD_Connection *connection) {
    const char *session = MHD_lookup_connection_value(connection, 
                                                    MHD_COOKIE_KIND, 
                                                    "session");
    return session != NULL;
}

static json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version,
                                   RouteParams *params) {
    (void)arena;
    json_t *context = json_object();

    // Add method, url and version to context
    json_object_set_new(context, "method", json_string(method));
    json_object_set_new(context, "url", json_string(url));
    json_object_set_new(context, "version", json_string(version));
    
    // Add auth status to context
    json_object_set_new(context, "isLoggedIn", json_boolean(isLoggedIn(connection)));
    
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

static bool verifyPassword(const char *password, const char *storedHash) {
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // Same fixed salt as in hashPassword
    
    int result = argon2id_hash_raw(
        2,      // iterations
        1 << 16, // 64MB memory
        1,      // parallelism
        password, strlen(password),
        salt, sizeof(salt),
        hash, sizeof(hash)
    );
    
    if (result != ARGON2_OK) {
        fprintf(stderr, "Failed to hash password: %s\n", argon2_error_message(result));
        return false;
    }
    
    // Convert hash to hex string for comparison
    char hashStr[65];
    for (size_t i = 0; i < sizeof(hash); i++) {
        sprintf(&hashStr[i * 2], "%02x", hash[i]);
    }
    hashStr[64] = '\0';
    
    return strcmp(hashStr, storedHash) == 0;
}

static enum MHD_Result handleLoginRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;
    const char *password = NULL;

    // get login and password from post data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "login") == 0) {
            login = value;
        } else if (strcmp(key, "password") == 0) {
            password = value;
        }
    }

    if (!login || !password) {
        // TODO: Return error page
        return MHD_NO;
    }

    // Look up user in database
    const char *values[] = {login};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT id, password_hash FROM users WHERE login = $1",
        values, 1);

    if (!result || PQntuples(result) == 0) {
        fprintf(stderr, "Login failed - user not found: %s\n", login);
        if (result) PQclear(result);
        // TODO: Return error page
        return MHD_NO;
    }

    // Get stored password hash and user ID - copy them since we need them after PQclear
    char *storedHash = arenaDupString(post->arena, PQgetvalue(result, 0, 1));  // password_hash is column 1
    char *userId = arenaDupString(post->arena, PQgetvalue(result, 0, 0));      // id is column 0
    PQclear(result);

    // Verify password
    if (!verifyPassword(password, storedHash)) {
        fprintf(stderr, "Login failed - incorrect password for: %s\n", login);
        // TODO: Return error page
        return MHD_NO;
    }

    printf("Login successful for user: %s (id: %s)\n", login, userId);
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);

    // create session token
    const char *token = "mock_session_123";
    
    // Set session cookie
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict", token);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Set redirect header
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Add with other handlers
static enum MHD_Result handleLogoutRequest(struct MHD_Connection *connection) {
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Clear session cookie by setting it to empty with immediate expiry
    MHD_add_response_header(response, "Set-Cookie", 
                          "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    
    // Redirect to home page
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Add helper function for password hashing
static char* hashPassword(Arena *arena, const char *password) {
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // In production, generate random salt
    
    int result = argon2id_hash_raw(
        2,      // iterations
        1 << 16, // 64MB memory
        1,      // parallelism
        password, strlen(password),
        salt, sizeof(salt),
        hash, sizeof(hash)
    );
    
    if (result != ARGON2_OK) {
        fprintf(stderr, "Failed to hash password: %s\n", argon2_error_message(result));
        return NULL;
    }
    
    // Convert hash to hex string
    char *hashStr = arenaAlloc(arena, sizeof(hash) * 2 + 1);
    for (size_t i = 0; i < sizeof(hash); i++) {
        sprintf(&hashStr[i * 2], "%02x", hash[i]);
    }
    
    return hashStr;
}

static enum MHD_Result handleRegisterRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;
    const char *password = NULL;
    const char *confirm_password = NULL;

    // Get form data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "login") == 0) {
            login = value;
        } else if (strcmp(key, "password") == 0) {
            password = value;
        } else if (strcmp(key, "confirm_password") == 0) {
            confirm_password = value;
        }
    }

    // Print registration attempt for now
    printf("Registration attempt - login: %s\n", login ? login : "null");
    
    // Basic validation
    if (!login || !password || !confirm_password) {
        // TODO: Return error page
        return MHD_NO;
    }
    
    if (strcmp(password, confirm_password) != 0) {
        // TODO: Return error page with "Passwords don't match"
        return MHD_NO;
    }

    // Hash password
    char *passwordHash = hashPassword(post->arena, password);
    printf("Password hash: %s\n", passwordHash);
    if (!passwordHash) {
        return MHD_NO;
    }

    // Insert user into database
    const char *values[] = {login, passwordHash};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO users (login, password_hash) VALUES ($1, $2) RETURNING id",
        values, 2);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Failed to create user account\n");
        if (result) PQclear(result);
        return MHD_NO;
    }

    // Get the new user's ID
    const char *userId = PQgetvalue(result, 0, 0);
    PQclear(result);

    // Create mock session token
    const char *token = "mock_session_123";
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Set session cookie
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict", token);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Redirect to home page
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
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
            return handleLogoutRequest(connection);
        }
        
        // Handle register endpoint
        if (strcmp(url, "/register") == 0) {
            return handleRegisterRequest(ctx, connection, post);
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
        buildRequestContextJson(connection, requestArena, *con_cls, method, url,
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
    }

    // Handle based on route type
    switch (match.type) {
        case ROUTE_TYPE_API:
            return handleApiRequest(connection, match.endpoint.api, method, pipelineResult);

        case ROUTE_TYPE_PAGE:
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
