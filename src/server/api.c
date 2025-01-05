#include "api.h"
#include "handler.h"
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

// Helper callback for MHD_get_connection_values
static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
  (void)kind; // Suppress unused parameter warning
  json_t *obj = (json_t *)cls;
  json_object_set_new(obj, key, json_string(value));
  return MHD_YES;
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

char *generateApiResponse(Arena *arena, ApiEndpoint *endpoint, void *con_cls,
                          json_t *requestContext) {
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
                                 ApiEndpoint *api, const char *method,
                                 const char *url, const char *version,
                                 void *con_cls, Arena *arena) {
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

  json_t *request_context =
      buildRequestContextJson(connection, arena, con_cls, method, url, version);

  // printf("Request context: %s\n", request_context);

  // Generate API response with request context
  char *json = generateApiResponse(arena, api, con_cls, request_context);
  if (!json) {
    const char *error_msg =
        "{ \"error\": \"Internal server error processing pipeline\" }";
    struct MHD_Response *response =
        createErrorResponse(error_msg, MHD_HTTP_INTERNAL_SERVER_ERROR);
    enum MHD_Result ret = MHD_queue_response(
        connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
    MHD_destroy_response(response);
    return ret;
  }

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

  return MHD_queue_response(connection, MHD_HTTP_OK, response);
}
