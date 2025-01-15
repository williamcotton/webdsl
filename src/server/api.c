#include "api.h"
#include "handler.h"
#include "db.h"
#include "validation.h"
#include "pipeline_executor.h"
#include <jq.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <pthread.h>
#include <uthash.h>
#include <math.h>
#include "lua.h"
#include "utils.h"
#include "jq.h"
#include "route_params.h"

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

// Helper callback for MHD_get_connection_values
static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
  (void)kind; // Suppress unused parameter warning
  json_t *obj = (json_t *)cls;
  json_object_set_new(obj, key, json_string(value));
  return MHD_YES;
}

json_t* buildRequestContextJson(struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version,
                                   RouteParams *params) {
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

static json_t* validatePostFields(Arena *arena, ApiEndpoint *endpoint, void *con_cls) {
    struct PostContext *post_ctx = con_cls;
    json_t *errors_obj = json_object();
    json_t *error_fields = json_object();
    bool has_errors = false;

    // Validate each field
    ApiField *field = endpoint->apiFields;

    while (field) {
        const char *value = NULL;
        
        if (post_ctx->type == REQUEST_TYPE_JSON_POST && post_ctx->raw_json) {
            // Parse JSON data if not already parsed
            json_error_t json_error;
            json_t *json_body = json_loads(post_ctx->raw_json, 0, &json_error);
            if (json_body) {
                // Get value from JSON body
                json_t *json_value = json_object_get(json_body, field->name);
                if (json_value) {
                    if (json_is_string(json_value)) {
                        value = json_string_value(json_value);
                    } else if (json_is_number(json_value)) {
                        // Convert number to string for validation
                        char num_str[32];
                        snprintf(num_str, sizeof(num_str), "%.0f", json_number_value(json_value));
                        value = arenaDupString(arena, num_str);
                    }
                }
                json_decref(json_body);
            }
        } else if (post_ctx->type == REQUEST_TYPE_POST) {
            // Get value from form data
            for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
                if (strcmp(post_ctx->post_data.keys[i], field->name) == 0) {
                    value = post_ctx->post_data.values[i];
                    break;
                }
            }
        }

        char *error = validateField(arena, value, field);
        if (error) {
            json_object_set_new(error_fields, field->name, json_string(error));
            has_errors = true;
        }

        field = field->next;
    }

    if (has_errors) {
        json_object_set_new(errors_obj, "errors", error_fields);
        return errors_obj;
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
    ApiField *field = endpoint->apiFields;

    while (field) {
        const char *value = NULL;
        
        if (post_ctx->type == REQUEST_TYPE_JSON_POST && post_ctx->raw_json) {
            // Parse JSON data
            json_error_t json_error;
            json_t *json_body = json_loads(post_ctx->raw_json, 0, &json_error);
            if (json_body) {
                // Get value from JSON body
                json_t *json_value = json_object_get(json_body, field->name);
                if (json_value) {
                    if (json_is_string(json_value)) {
                        value = arenaDupString(arena, json_string_value(json_value));
                    } else if (json_is_number(json_value)) {
                        char num_str[32];
                        snprintf(num_str, sizeof(num_str), "%.0f", json_number_value(json_value));
                        value = arenaDupString(arena, num_str);
                    }
                }
                json_decref(json_body);
            }
        } else if (post_ctx->type == REQUEST_TYPE_POST) {
            // Get value from form data
            for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
                if (strcmp(post_ctx->post_data.keys[i], field->name) == 0) {
                    value = post_ctx->post_data.values[i];
                    break;
                }
            }
        }
        
        (*values)[(*value_count)++] = value;
        field = field->next;
    }
}

json_t *generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls,
                          json_t *requestContext, ServerContext *ctx) {
    // For POST requests, validate fields if specified
    if (endpoint->apiFields && strcmp(endpoint->method, "POST") == 0) {
        json_t *validation_error = validatePostFields(arena, endpoint, con_cls);
        if (validation_error) {
            return validation_error;
        }

        // Extract values from POST data
        const char **values = NULL;
        size_t value_count = 0;
        extractPostValues(arena, endpoint, con_cls, &values, &value_count);

        // Add extracted values to request context
        json_t *body = json_object_get(requestContext, "body");
        if (body && json_is_object(body)) {
            ApiField *field = endpoint->apiFields;
            for (size_t i = 0; i < value_count && field; i++, field = field->next) {
                if (values[i]) {
                    json_object_set_new(body, field->name, json_string(values[i]));
                }
            }
        }
    }

    if (endpoint->uses_pipeline) {
        // Execute pipeline with validated request context
        json_t *result = executePipeline(ctx, endpoint->pipeline, requestContext, arena);
        if (!result) {
            return generateErrorJson("Pipeline execution failed");
        }

        json_t *error = json_object_get(result, "error");
        if (error) {
            // Extract just the error and statusCode
            json_t *error_response = json_object();
            json_t *statusCode = json_object_get(result, "statusCode");
            json_object_set(error_response, "error", error);
            json_object_set(error_response, "statusCode", statusCode);
            return error_response;
        }

        return result;
    } else {
        return generateErrorJson("No pipeline found");
    }
}

enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                                 ApiEndpoint *api, const char *method,
                                 void *con_cls, Arena *arena,
                                 json_t *pipelineResult) {
  // Handle OPTIONS requests for CORS
  if (strcmp(method, "OPTIONS") == 0) {
    struct MHD_Response *response =
        MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods",
                            "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers",
                            "Content-Type");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
  }

  // Verify HTTP method matches (but allow GET for GET endpoints)
  if (strcmp(method, api->method) != 0 &&
      !(strcmp(method, "GET") == 0 && strcmp(api->method, "GET") == 0)) {
    const char *method_not_allowed = "{ \"error\": \"Method not allowed\" }";
    char *error = strdup(method_not_allowed);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(error), error, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "application/json");
    enum MHD_Result ret =
        MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
    MHD_destroy_response(response);
    return ret;
  }

  // For POST requests, validate fields if specified
  if (api->apiFields && strcmp(method, "POST") == 0) {
    json_t *validation_error = validatePostFields(arena, api, con_cls);
    if (validation_error) {
      char *error_str = json_dumps(validation_error, 0);
      struct MHD_Response *response = MHD_create_response_from_buffer(
          strlen(error_str), error_str, MHD_RESPMEM_MUST_FREE);
      MHD_add_response_header(response, "Content-Type", "application/json");
      enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
      MHD_destroy_response(response);
      return ret;
    }
  }

  // Use the passed-in pipeline result
  json_t *apiResponse = pipelineResult;
  if (!apiResponse) {
    const char *error_msg =
        "{ \"error\": \"Internal server error processing pipeline\" }";
    struct MHD_Response *response =
        createErrorResponse(error_msg, MHD_HTTP_INTERNAL_SERVER_ERROR);
    enum MHD_Result ret = MHD_queue_response(
        connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
    MHD_destroy_response(response);
    return ret;
  }

  // check if apiResponse is an error
  json_t *error = json_object_get(apiResponse, "error");
  if (error) {
    json_t *statusCodeJson = json_object_get(apiResponse, "statusCode");
    unsigned int statusCode = MHD_HTTP_BAD_REQUEST;
    if (json_is_number(statusCodeJson)) {
        statusCode = (unsigned int)llrint(json_number_value(statusCodeJson));
    }
    json_object_del(apiResponse, "statusCode");
    char *error_str = json_dumps(apiResponse, 0);
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error_str), error_str, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret = MHD_queue_response(connection, statusCode, response);
    MHD_destroy_response(response);
    return ret;
  }

  char *json = json_dumps(apiResponse, 0);

  // Use arena to allocate the response copy
  size_t json_len = strlen(json);

  struct MHD_Response *response =
      MHD_create_response_from_buffer(json_len, json, MHD_RESPMEM_PERSISTENT);
  MHD_add_response_header(response, "Content-Type", "application/json");

  // Add CORS headers for API endpoints
  MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
  MHD_add_response_header(response, "Access-Control-Allow-Methods",
                          "GET, POST, OPTIONS");
  MHD_add_response_header(response, "Access-Control-Allow-Headers",
                          "Content-Type");

  enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;
}
