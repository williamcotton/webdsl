#include "api.h"
#include "handler.h"
#include "routing.h"
#include "db.h"
#include <jq.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <pthread.h>
#include <uthash.h>
#include "lua.h"
#include "utils.h"
#include "jq.h"

extern Arena *serverArena;
extern Database *db;

static json_t* executeAndFormatQuery(Arena *arena, QueryNode *query, 
                                   const char **values, size_t value_count);
static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value);


// Fix the const qualifier drop warning
static struct MHD_Response* createErrorResponse(const char *error_msg, int status_code) {
    (void)status_code;
    size_t len = strlen(error_msg);
    char *error_copy = malloc(len + 1);
    memcpy(error_copy, error_msg, len + 1);
    
    struct MHD_Response *response = MHD_create_response_from_buffer(len, error_copy,
                                                                  MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    return response;
}

static json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version);

static json_t* executeAndFormatQuery(Arena *arena, QueryNode *query, 
                                   const char **values, size_t value_count) {
    (void)arena;
    
    if (!query) {
        return NULL;
    }

    // Execute query
    PGresult *result;
    if (values && value_count > 0) {
        result = executeParameterizedQuery(db, query->sql, values, value_count);
    } else {
        result = executeQuery(db, query->sql);
    }

    if (!result) {
        return NULL;
    }

    // Convert result to JSON
    json_t *jsonData = resultToJson(result);
    freeResult(result);

    if (!jsonData) {
        return NULL;
    }

    return jsonData;
}

static json_t* executeSqlStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena) {
    (void)requestContext;
    
    if (step->is_dynamic) {
        // For dynamic SQL, expect input to contain SQL and params
        const char *sql = json_string_value(json_object_get(input, "sql"));
        if (!sql) {
            return NULL;
        }
        
        json_t *params = json_object_get(input, "params");
        const char **param_values = NULL;
        size_t param_count = 0;
        
        if (json_is_array(params)) {
            param_count = json_array_size(params);
            if (param_count > 0) {
                param_values = arenaAlloc(arena, sizeof(char*) * param_count);
                
                for (size_t i = 0; i < param_count; i++) {
                    json_t *param = json_array_get(params, i);
                    if (json_is_string(param)) {
                        param_values[i] = json_string_value(param);
                    } else {
                        // For non-string values, convert to string
                        char *str = json_dumps(param, JSON_COMPACT);
                        if (str) {
                            param_values[i] = arenaDupString(arena, str);
                            free(str);
                        }
                    }
                }
            }
        }
        
        PGresult *result = executeParameterizedQuery(db, sql, param_values, param_count);
        if (!result) {
            return NULL;
        }
        
        json_t *jsonResult = resultToJson(result);
        freeResult(result);

        // add the input to the result
        if (input) {
            json_object_set(jsonResult, "request", input);
        }
        
        if (!jsonResult) {
            fprintf(stderr, "Failed to convert SQL result to JSON\n");
        }
        return jsonResult;
    } else {
        // For static SQL, look up the query and execute it
        QueryNode *query = findQuery(step->name);
        if (!query) {
            return NULL;
        }
        
        // Extract parameters from input if needed
        const char **values = NULL;
        size_t value_count = 0;
        
        // Check if input has a params array
        if (input) {
            json_t *params = json_object_get(input, "params");
            if (json_is_array(params)) {
                value_count = json_array_size(params);
                if (value_count > 0) {
                    values = arenaAlloc(arena, sizeof(char*) * value_count);
                    for (size_t i = 0; i < value_count; i++) {
                        json_t *param = json_array_get(params, i);
                        if (json_is_string(param)) {
                            values[i] = json_string_value(param);
                        } else {
                            // For non-string values, convert to string
                            char *str = json_dumps(param, JSON_COMPACT);
                            if (str) {
                                values[i] = arenaDupString(arena, str);
                                free(str);
                            }
                        }
                    }
                }
            } else if (json_is_array(input)) {
                // If input itself is an array, use it directly as params
                value_count = json_array_size(input);
                if (value_count > 0) {
                    values = arenaAlloc(arena, sizeof(char*) * value_count);
                    for (size_t i = 0; i < value_count; i++) {
                        json_t *param = json_array_get(input, i);
                        if (json_is_string(param)) {
                            values[i] = json_string_value(param);
                        } else {
                            // For non-string values, convert to string
                            char *str = json_dumps(param, JSON_COMPACT);
                            if (str) {
                                values[i] = arenaDupString(arena, str);
                                free(str);
                            }
                        }
                    }
                }
            }
        }
        
        json_t *jsonData = executeAndFormatQuery(arena, query, values, value_count);
        if (!jsonData) {
            return NULL;
        }

        // add the input to the result
        if (input) {
            json_object_set(jsonData, "request", input);
        }

        return jsonData;
    }
}

// Function to set up the executor based on step type
void setupStepExecutor(PipelineStepNode *step) {
    switch (step->type) {
        case STEP_JQ:
            step->execute = executeJqStep;
            break;
        case STEP_LUA:
            step->execute = executeLuaStep;
            break;
        case STEP_SQL:
        case STEP_DYNAMIC_SQL:
            step->execute = executeSqlStep;
            break;
    }
}

// Simplified executePipelineStep that just uses the function pointer
static json_t* executePipelineStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena) {
    if (!step || !step->execute) {
        return NULL;
    }
    return step->execute(step, input, requestContext, arena);
}

static json_t* executePipeline(ApiEndpoint *endpoint, json_t *requestContext, Arena *arena) {
    json_t *current = requestContext;
    PipelineStepNode *step = endpoint->pipeline;
    
    while (step) {
        json_t *result = executePipelineStep(step, current, requestContext, arena);
        if (current != requestContext) {
            json_decref(current);
        }
        if (!result) {
            return NULL;
        }
        current = result;
        step = step->next;
    }
    
    return current;
}

char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls, json_t *requestContext) {
    (void)con_cls;
    if (endpoint->uses_pipeline) {
        // New pipeline execution path
        json_t *result = executePipeline(endpoint, requestContext, arena);
        if (!result) {
            return generateErrorJson("Pipeline execution failed");
        }
        return json_dumps(result, JSON_COMPACT);
    } else {
        return generateErrorJson("No pipeline found");
    }
}

enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               const char *url,
                               const char *version,
                               void *con_cls,
                               Arena *arena) {
    // Handle OPTIONS requests for CORS
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Verify HTTP method matches (but allow GET for GET endpoints)
    if (strcmp(method, api->method) != 0 && 
        !(strcmp(method, "GET") == 0 && strcmp(api->method, "GET") == 0)) {
        const char *method_not_allowed = "{ \"error\": \"Method not allowed\" }";
        char *error = strdup(method_not_allowed);
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error), error,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
        MHD_destroy_response(response);
        return ret;
    }

    json_t *request_context = buildRequestContextJson(connection, arena, con_cls,
                                                  method, url, version);

    // printf("Request context: %s\n", request_context);
    
    // Generate API response with request context
    char *json = generateApiResponse(arena, api, con_cls, request_context);
    if (!json) {
        const char *error_msg = "{ \"error\": \"Internal server error processing JQ filter\" }";
        struct MHD_Response *response = createErrorResponse(error_msg, MHD_HTTP_INTERNAL_SERVER_ERROR);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Use arena to allocate the response copy
    size_t json_len = strlen(json);
    
    struct MHD_Response *response = MHD_create_response_from_buffer(
        json_len,
        json,
        MHD_RESPMEM_PERSISTENT
    );
    MHD_add_response_header(response, "Content-Type", "application/json");
    
    // Add CORS headers for API endpoints
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
    
    return MHD_queue_response(connection, MHD_HTTP_OK, response);
}

static json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version) {
    (void)arena; // Suppress unused parameter warning
    json_t *context = json_object();

    // Add method, url and version to context
    json_object_set_new(context, "method", json_string(method));
    json_object_set_new(context, "url", json_string(url));
    json_object_set_new(context, "version", json_string(version));
    
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

    // Build body object
    json_t *body = json_object();
    if (strcmp(method, "POST") == 0) {
        struct PostContext *post_ctx = con_cls;
        if (post_ctx && post_ctx->type == REQUEST_TYPE_POST) {
            // loop over post_ctx->post_data.values and add to body
            for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
                const char *value = post_ctx->post_data.values[i];
                const char *key = post_ctx->post_data.keys[i];
                if (value) {
                    json_object_set_new(body, key, json_string(value));
                }
            }
        }
    }
    json_object_set_new(context, "body", body);

    return context;
}

// Helper callback for MHD_get_connection_values
static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value) {
    (void)kind; // Suppress unused parameter warning
    json_t *obj = (json_t*)cls;
    json_object_set_new(obj, key, json_string(value));
    return MHD_YES;
}
