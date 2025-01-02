#include "api.h"
#include "handler.h"
#include "routing.h"
#include "validation.h"
#include "../db.h"
#include <jq.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <pthread.h>
#include <uthash.h>
#include <stdint.h>

extern Arena *serverArena;
extern Database *db;

// Forward declarations of static functions
static char* validatePostFields(Arena *arena, ApiEndpoint *endpoint, void *con_cls);
static void extractPostValues(Arena *arena, ApiEndpoint *endpoint, void *con_cls,
                            const char ***values, size_t *value_count);
static void extractFilterValues(Arena *arena, const char *filtered,
                              const char ***values, size_t *value_count);
static char* formatResponse(Arena *arena, json_t *jsonData, const char *jqFilter);
static jv processJqFilter(jq_state *jq, json_t *input_json);
static json_t* executeAndFormatQuery(Arena *arena, QueryNode *query, 
                                   const char **values, size_t value_count);
static enum MHD_Result json_kv_iterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value);
static jv jansson_to_jv(json_t *json);

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

static char* generateErrorJson(const char *errorMessage);
static json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version);
static enum MHD_Result json_kv_iterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value);
static jv jansson_to_jv(json_t *json);

static jv processJqFilter(jq_state *jq, json_t *input_json) {
    if (!jq || !input_json) return jv_invalid();

    // Convert to JV format
    jv input = jansson_to_jv(input_json);
    if (!jv_is_valid(input)) {
        jv jv_error = jv_invalid_get_msg(input);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JSON conversion error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        jv_free(input);
        return jv_invalid();
    }

    // Process the filter
    jq_start(jq, input, 0);
    jv filtered_result = jq_next(jq);

    if (!jv_is_valid(filtered_result)) {
        jv jv_error = jv_invalid_get_msg(filtered_result);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JQ execution error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        return filtered_result; // Already invalid
    }

    return filtered_result;
}

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

char* generateApiResponse(Arena *arena, 
                        ApiEndpoint *endpoint, 
                        void *con_cls,
                        json_t *requestContext) {
    // Handle POST requests
    if (strcmp(endpoint->method, "POST") == 0 && endpoint->fields) {
        // Validate fields
        char *validation_error = validatePostFields(arena, endpoint, con_cls);
        if (validation_error) {
            return validation_error;
        }

        // Extract values from POST data
        const char **values;
        size_t value_count;
        extractPostValues(arena, endpoint, con_cls, &values, &value_count);

        // Execute query and get JSON result
        QueryNode *query = findQuery(endpoint->jsonResponse);
        if (!query) {
            return NULL;
        }

        json_t *jsonData = executeAndFormatQuery(arena, query, values, value_count);
        if (!jsonData) {
            return NULL;
        }

        // Format and return response
        return formatResponse(arena, jsonData, endpoint->jqFilter);
    }

    // Handle GET requests
    
    // Process preFilter if exists
    const char **values = NULL;
    size_t value_count = 0;
    
    if (endpoint->preJqFilter) {
        jq_state *pre_jq = findOrCreateJQ(endpoint->preJqFilter);
        if (!pre_jq) {
            return generateErrorJson("Failed to create preFilter");
        }

        jv filtered_jv = processJqFilter(pre_jq, requestContext);
        if (!jv_is_valid(filtered_jv)) {
            return generateErrorJson("Failed to apply preFilter");
        }

        // Convert JQ result to string for parameter extraction
        jv dumped = jv_dump_string(filtered_jv, 0);
        const char *filtered = jv_string_value(dumped);
        char *filtered_copy = arenaDupString(arena, filtered);
        jv_free(dumped);
        jv_free(filtered_jv);

        // Extract values from preFilter result
        extractFilterValues(arena, filtered_copy, &values, &value_count);
    }

    // Execute main query
    QueryNode *query = findQuery(endpoint->jsonResponse);
    if (!query) {
        return NULL;
    }

    json_t *jsonData = executeAndFormatQuery(arena, query, values, value_count);
    if (!jsonData) {
        return NULL;
    }

    // Apply postFilter and format response
    char *response = formatResponse(arena, jsonData, endpoint->jqFilter);
    return response;
}

static char* formatResponse(Arena *arena, json_t *jsonData, const char *jqFilter) {
    if (!jsonData) return NULL;

    if (jqFilter) {
        jq_state *jq = findOrCreateJQ(jqFilter);
        if (!jq) {
            json_decref(jsonData);
            return NULL;
        }

        jv filtered_jv = processJqFilter(jq, jsonData);
        json_decref(jsonData);

        if (!jv_is_valid(filtered_jv)) {
            return NULL;
        }

        // Convert JQ result to string
        jv dumped = jv_dump_string(filtered_jv, 0);
        const char *str = jv_string_value(dumped);
        char *response = arenaDupString(arena, str);
        jv_free(dumped);
        jv_free(filtered_jv);
        return response;
    }

    // No filter - just convert to string
    char *jsonString = json_dumps(jsonData, JSON_COMPACT);
    char *response = arenaDupString(arena, jsonString);
    free(jsonString);
    json_decref(jsonData);
    return response;
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

    // Build request context JSON for preFilter
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

// Add helper function for error responses
static char* generateErrorJson(const char *errorMessage) {
    json_t *root = json_object();
    json_object_set_new(root, "error", json_string(errorMessage));
    
    char *jsonStr = json_dumps(root, JSON_COMPACT);
    return jsonStr;
}

// static char* applyJqFilterToJson(Arena *arena, const char *json, const char *filter) {
//     jq_state *jq = findOrCreateJQ(filter);
//     if (!jq) {
//         return NULL;
//     }

//     // Parse input JSON
//     jv input = jv_parse(json);
//     if (!jv_is_valid(input)) {
//         jv error = jv_invalid_get_msg(input);
//         if (jv_is_valid(error)) {
//             fprintf(stderr, "JQ parse error: %s\n", jv_string_value(error));
//             jv_free(error);
//         }
//         jv_free(input);
//         return NULL;
//     }

//     // Process the filter
//     jq_start(jq, input, 0);
//     jv jq_result = jq_next(jq);

//     if (!jv_is_valid(jq_result)) {
//         jv error = jv_invalid_get_msg(jq_result);
//         if (jv_is_valid(error)) {
//             fprintf(stderr, "JQ execution error: %s\n", jv_string_value(error));
//             jv_free(error);
//         }
//         jv_free(jq_result);
//         jv_free(input);
//         return NULL;
//     }

//     // Dump the result to a string
//     jv dumped = jv_dump_string(jq_result, 0);
//     const char *str = jv_string_value(dumped);
//     char *filtered = arenaDupString(arena, str);
//     jv_free(dumped);
//     jv_free(jq_result);
//     jv_free(input);
    
//     return filtered;
// }

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
        json_kv_iterator, query);
    json_object_set_new(context, "query", query);
    
    // Build headers object
    json_t *headers = json_object();
    MHD_get_connection_values(connection, MHD_HEADER_KIND,
        json_kv_iterator, headers);
    json_object_set_new(context, "headers", headers);
    
    // Build cookies object
    json_t *cookies = json_object();
    MHD_get_connection_values(connection, MHD_COOKIE_KIND,
        json_kv_iterator, cookies);
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
    
    // Convert to string
    // char *json_str = json_dumps(context, JSON_COMPACT);
    // json_decref(context);
    // return json_str;
    return context;
}

// Helper callback for MHD_get_connection_values
static enum MHD_Result json_kv_iterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value) {
    (void)kind; // Suppress unused parameter warning
    json_t *obj = (json_t*)cls;
    json_object_set_new(obj, key, json_string(value));
    return MHD_YES;
}

static jv jansson_to_jv(json_t *json) {
    switch (json_typeof(json)) {
        case JSON_OBJECT: {
            jv obj = jv_object();
            const char *key;
            json_t *value;
            json_object_foreach(json, key, value) {
                obj = jv_object_set(obj, jv_string(key), jansson_to_jv(value));
            }
            return obj;
        }
        case JSON_ARRAY: {
            jv arr = jv_array();
            size_t index;
            json_t *value;
            json_array_foreach(json, index, value) {
                arr = jv_array_append(arr, jansson_to_jv(value));
            }
            return arr;
        }
        case JSON_STRING:
            return jv_string(json_string_value(json));
        case JSON_INTEGER: {
            json_int_t val = json_integer_value(json);
            // Use explicit cast to avoid implicit conversion warning
            return jv_number((double)val);
        }
        case JSON_REAL:
            return jv_number(json_real_value(json));
        case JSON_TRUE:
            return jv_true();
        case JSON_FALSE:
            return jv_false();
        case JSON_NULL:
            return jv_null();
        default:
            return jv_null();
    }
}

static char* validatePostFields(Arena *arena, ApiEndpoint *endpoint, void *con_cls) {
    struct PostContext *post_ctx = con_cls;
    json_t *errors_obj = json_object();
    json_t *error_fields = json_object();
    bool has_errors = false;

    // Validate each field
    ApiField *field = endpoint->apiFields;
    size_t field_index = 0;
    
    while (field) {
        const char *value = NULL;
        if (field_index < post_ctx->post_data.value_count) {
            value = post_ctx->post_data.values[field_index];
        }

        char *error = validateField(arena, value, field);
        if (error) {
            json_object_set_new(error_fields, field->name, json_string(error));
            has_errors = true;
        }
        
        field_index++;
        field = field->next;
    }
    
    if (has_errors) {
        json_object_set_new(errors_obj, "errors", error_fields);
        char *json_str = json_dumps(errors_obj, JSON_INDENT(2));
        json_decref(errors_obj);
        return json_str;
    }

    json_decref(errors_obj);
    return NULL;
}

static void extractPostValues(Arena *arena, ApiEndpoint *endpoint, void *con_cls,
                            const char ***values, size_t *value_count) {
    struct PostContext *post_ctx = con_cls;
    *values = arenaAlloc(arena, sizeof(char*) * 32);
    *value_count = 0;
    
    // Extract validated fields for query
    ResponseField *resp_field = endpoint->fields;
    size_t field_index = 0;
    
    while (resp_field) {
        const char *value = NULL;
        if (field_index < post_ctx->post_data.value_count) {
            value = post_ctx->post_data.values[field_index++];
        }
        (*values)[(*value_count)++] = value;
        resp_field = resp_field->next;
    }
}

static void extractFilterValues(Arena *arena, const char *filtered,
                              const char ***values, size_t *value_count) {
    json_error_t error;
    json_t *params = json_loads(filtered, 0, &error);
    if (!params || !json_is_object(params)) {
        *values = NULL;
        *value_count = 0;
        return;
    }

    *value_count = json_object_size(params);
    *values = arenaAlloc(arena, sizeof(char*) * (*value_count));
    
    const char *key;
    json_t *value;
    size_t index = 0;
    
    json_object_foreach(params, key, value) {
        if (json_is_string(value)) {
            (*values)[index++] = arenaDupString(arena, json_string_value(value));
        } else {
            char *str = json_dumps(value, JSON_COMPACT);
            if (str) {
                (*values)[index++] = arenaDupString(arena, str);
                free(str);
            } else {
                (*values)[index++] = arenaDupString(arena, "{}");
            }
        }
    }
    
    json_decref(params);
}
