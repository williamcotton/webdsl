#include "api.h"
#include "handler.h"
#include "routing.h"
#include "validation.h"
#include "../db.h"
#include "../stringbuilder.h"
#include "../jq_wrapper.h"
#include <string.h>
#include <stdio.h>
#include <jansson.h>

extern Arena *serverArena;
extern Database *db;

static char* generateErrorJson(Arena *arena, const char *errorMessage);
static char* applyJqFilterToJson(Arena *arena, const char *json, jq_state *jq);

char* generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls) {
    // For POST requests with form data
    if (strcmp(endpoint->method, "POST") == 0 && endpoint->fields) {
        struct PostContext *post_ctx = con_cls;
        StringBuilder *errors = StringBuilder_new(arena);
        StringBuilder_append(errors, "{\n  \"errors\": {\n");
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
                if (has_errors) {
                    StringBuilder_append(errors, ",\n");
                }
                StringBuilder_append(errors, "    \"%s\": \"%s\"", field->name, error);
                has_errors = true;
            }
            
            field_index++;
            field = field->next;
        }
        
        StringBuilder_append(errors, "\n  }\n}");
        
        if (has_errors) {
            return arenaDupString(arena, StringBuilder_get(errors));
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
            return generateErrorJson(arena, "Query not found");
        }

        PGresult *result = executeParameterizedQuery(db, query->sql, values, value_count);
        if (!result) {
            return generateErrorJson(arena, getDatabaseError(db));
        }

        char *json = resultToJson(arena, result);
        freeResult(result);

        // Apply JQ filter if specified in endpoint
        if (endpoint->jqFilter) {
            // Initialize JQ once per endpoint
            static __thread jq_state *jq = NULL;
            if (!jq) {
                jq = jq_init();
                if (jq && endpoint->jqFilter) {
                    // Compile filter once
                    jq_compile(jq, endpoint->jqFilter);
                }
            }

            if (jq) {
                char *filtered = applyJqFilterToJson(arena, json, jq);
                if (filtered) {
                    return filtered;
                }
            }
            // Fall back to unfiltered JSON on error
        }

        return json;
    }

    // Regular GET request handling
    QueryNode *query = findQuery(endpoint->jsonResponse);
    if (!query) {
        return generateErrorJson(arena, "Query not found");
    }

    PGresult *result = executeQuery(db, query->sql);
    if (!result) {
        return generateErrorJson(arena, getDatabaseError(db));
    }

    char *json = resultToJson(arena, result);
    freeResult(result);

    // Only compile and apply JQ filter if one is specified
    if (endpoint->jqFilter) {
        // Initialize JQ once per endpoint
        static __thread jq_state *jq = NULL;
        if (!jq) {
            jq = jq_init();
            if (jq && endpoint->jqFilter) {
                // Compile filter once
                jq_compile(jq, endpoint->jqFilter);
            }
        }

        if (jq) {
            char *filtered = applyJqFilterToJson(arena, json, jq);
            if (filtered) {
                return filtered;
            }
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

    // Generate API response with form data
    char *json = generateApiResponse(arena, api, con_cls);
    char *json_copy = strdup(json);
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(json_copy), json_copy,
                                               MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "application/json");
    
    // Add CORS headers for API endpoints
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
    
    return MHD_queue_response(connection, MHD_HTTP_OK, response);
}

// Add helper function for error responses
static char* generateErrorJson(Arena *arena, const char *errorMessage) {
    json_t *root = json_object();
    json_object_set_new(root, "error", json_string(errorMessage));
    
    char *jsonStr = json_dumps(root, JSON_COMPACT);
    char *resultStr = arenaDupString(arena, jsonStr);
    
    free(jsonStr);
    json_decref(root);
    
    return resultStr;
}

static char* applyJqFilterToJson(Arena *arena, const char *json, jq_state *jq) {
    // Parse input JSON
    jv input = jv_parse(json);
    if (jv_is_valid(input)) {
        // Process the filter
        jq_start(jq, input, 0);
        jv jq_result = jq_next(jq);
        
        if (jv_is_valid(jq_result)) {
            // Dump the result to a string
            jv dumped = jv_dump_string(jq_result, 0);
            const char *str = jv_string_value(dumped);
            char *filtered = arenaDupString(arena, str);
            jv_free(dumped);
            jv_free(jq_result);
            return filtered;
        }
        jv_free(jq_result);
    }
    jv_free(input);
    return NULL;
}
