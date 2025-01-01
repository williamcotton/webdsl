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

static char* generateErrorJson(const char *errorMessage);
static char* applyJqFilterToJson(Arena *arena, const char *json, const char *filter);
static char* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, void *con_cls, const char *method);
static enum MHD_Result json_kv_iterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value);

char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls) {
    // For POST requests with form data
    if (strcmp(endpoint->method, "POST") == 0 && endpoint->fields) {
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

        // If validation passed, proceed with database operation
        const char **values = arenaAlloc(arena, sizeof(char*) * 32);
        size_t value_count = 0;
        
        // Extract validated fields for query
        ResponseField *resp_field = endpoint->fields;
        field_index = 0;
        
        while (resp_field) {
            const char *value = NULL;
            if (field_index < post_ctx->post_data.value_count) {
                value = post_ctx->post_data.values[field_index++];
            }
            values[value_count++] = value;
            resp_field = resp_field->next;
        }

        // Execute parameterized query
        QueryNode *query = findQuery(endpoint->jsonResponse);
        if (!query) {
            return generateErrorJson("Query not found");
        }

        PGresult *result = executeParameterizedQuery(db, query->sql, values, value_count);
        if (!result) {
            const char *dbError = getDatabaseError(db);
            json_t *error = json_object();
            json_object_set_new(error, "error", json_string_nocheck(dbError));
            char *jsonStr = json_dumps(error, JSON_COMPACT);
            return jsonStr;
        }

        char *json = resultToJson(arena, result);
        freeResult(result);

        // Apply JQ filter if specified in endpoint
        if (endpoint->jqFilter) {
            char *filtered = applyJqFilterToJson(arena, json, endpoint->jqFilter);
            if (filtered) {
                return filtered;
            }
            // Fall back to unfiltered JSON on error
        }

        return json;
    }

    // Regular GET request handling
    QueryNode *query = findQuery(endpoint->jsonResponse);
    if (!query) {
        return generateErrorJson("Query not found");
    }

    PGresult *result = executeQuery(db, query->sql);
    if (!result) {
        return generateErrorJson(getDatabaseError(db));
    }

    char *json = resultToJson(arena, result);
    freeResult(result);

    // Only compile and apply JQ filter if one is specified
    if (endpoint->jqFilter) {
        char *filtered = applyJqFilterToJson(arena, json, endpoint->jqFilter);
        if (filtered) {
            return filtered;
        }
        // Fall back to unfiltered JSON on error
    }

    return json;
}

enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
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
    char *request_context = buildRequestContextJson(connection, arena, con_cls, method);

    printf("Request context: %s\n", request_context);
    
    // Generate API response with form data
    char *json = generateApiResponse(arena, api, con_cls);
    
    // Use arena to allocate the response copy
    size_t json_len = strlen(json);
    char *json_copy = arenaAlloc(arena, json_len + 1);
    memcpy(json_copy, json, json_len + 1);
    
    struct MHD_Response *response = MHD_create_response_from_buffer(
        json_len,
        json_copy,
        MHD_RESPMEM_PERSISTENT  // Changed from MHD_RESPMEM_MUST_FREE
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

static char* applyJqFilterToJson(Arena *arena, const char *json, const char *filter) {
    jq_state *jq = findOrCreateJQ(filter);
    if (!jq) {
        return NULL;
    }

    // Parse input JSON
    jv input = jv_parse(json);
    if (!jv_is_valid(input)) {
        jv error = jv_invalid_get_msg(input);
        if (jv_is_valid(error)) {
            fprintf(stderr, "JQ parse error: %s\n", jv_string_value(error));
            jv_free(error);
        }
        jv_free(input);
        return NULL;
    }

    // Process the filter
    jq_start(jq, input, 0);
    jv jq_result = jq_next(jq);

    if (!jv_is_valid(jq_result)) {
        jv error = jv_invalid_get_msg(jq_result);
        if (jv_is_valid(error)) {
            fprintf(stderr, "JQ execution error: %s\n", jv_string_value(error));
            jv_free(error);
        }
        jv_free(jq_result);
        jv_free(input);
        return NULL;
    }

    // Dump the result to a string
    jv dumped = jv_dump_string(jq_result, 0);
    const char *str = jv_string_value(dumped);
    char *filtered = arenaDupString(arena, str);
    jv_free(dumped);
    jv_free(jq_result);
    jv_free(input);
    
    return filtered;
}

static char* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, void *con_cls, const char *method) {
    (void)arena; // Suppress unused parameter warning
    json_t *context = json_object();

    // Add method to context
    json_object_set_new(context, "method", json_string(method));
    
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
    char *json_str = json_dumps(context, JSON_COMPACT);
    json_decref(context);
    return json_str;
}

// Helper callback for MHD_get_connection_values
static enum MHD_Result json_kv_iterator(void *cls, enum MHD_ValueKind kind, 
                                      const char *key, const char *value) {
    (void)kind; // Suppress unused parameter warning
    json_t *obj = (json_t*)cls;
    json_object_set_new(obj, key, json_string(value));
    return MHD_YES;
}
