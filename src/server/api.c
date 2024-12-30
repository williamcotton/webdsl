#include "api.h"
#include "handler.h"
#include "routing.h"
#include "validation.h"
#include "../db.h"
#include "../stringbuilder.h"
#include <string.h>
#include <stdio.h>

extern Arena *serverArena;
extern Database *db;

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
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Query not found: %s\"\n", endpoint->jsonResponse);
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }

        PGresult *result = executeParameterizedQuery(db, query->sql, values, value_count);
        if (!result) {
            StringBuilder *sb = StringBuilder_new(arena);
            StringBuilder_append(sb, "{\n");
            StringBuilder_append(sb, "  \"error\": \"Query execution failed: %s\"\n", getDatabaseError(db));
            StringBuilder_append(sb, "}");
            return arenaDupString(arena, StringBuilder_get(sb));
        }

        char *json = resultToJson(arena, result);
        freeResult(result);
        return json;
    }

    // Regular GET request handling
    QueryNode *query = findQuery(endpoint->jsonResponse);
    if (!query) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Query not found: %s\"\n", endpoint->jsonResponse);
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    PGresult *result = executeQuery(db, query->sql);
    if (!result) {
        StringBuilder *sb = StringBuilder_new(arena);
        StringBuilder_append(sb, "{\n");
        StringBuilder_append(sb, "  \"error\": \"Query execution failed: %s\"\n", getDatabaseError(db));
        StringBuilder_append(sb, "}");
        return arenaDupString(arena, StringBuilder_get(sb));
    }

    char *json = resultToJson(arena, result);
    freeResult(result);
    return json;
}

enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                               ApiEndpoint *api,
                               const char *method,
                               void *con_cls) {
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
    char *json = generateApiResponse(serverArena, api, con_cls);
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
