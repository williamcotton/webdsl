#include "api.h"

#include <jansson.h>
#include <jq.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <uthash.h>

// Fix the const qualifier drop warning
static struct MHD_Response* createErrorResponse(const char *error_msg, int status_code) {
    (void)status_code;
    size_t len = strlen(error_msg);
    char *error_copy = malloc(len + 1);
    if (error_copy == NULL) {
        // If memory allocation fails, use a static error message instead
        char *fallback = "Internal server error - memory allocation failed";
        return MHD_create_response_from_buffer(strlen(fallback), fallback, 
                                             MHD_RESPMEM_PERSISTENT);
    }
    memcpy(error_copy, error_msg, len + 1);
    
    struct MHD_Response *response = MHD_create_response_from_buffer(len, error_copy,
                                                                  MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    return response;
}

enum MHD_Result handleApiRequest(struct MHD_Connection *connection,
                                 ApiEndpoint *api, const char *method,
                                 json_t *pipelineResult) {
  // Handle OPTIONS requests for CORS
  if (strcmp(method, "OPTIONS") == 0) {
    struct MHD_Response *response =
        MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods",
                            "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers",
                            "Content-Type");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
  }

  // Verify HTTP method matches
  if (strcmp(method, api->method) != 0) {
    const char *method_not_allowed = "{ \"error\": \"Method not allowed\" }";
    char *error = strdup(method_not_allowed);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(error), error, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret =
        MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
    MHD_destroy_response(response);
    return ret;
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

  struct MHD_Response *response =
      MHD_create_response_from_buffer(strlen(json), json, MHD_RESPMEM_PERSISTENT);
  MHD_add_response_header(response, "Content-Type", "application/json");

  // Add CORS headers for API endpoints
  MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
  MHD_add_response_header(response, "Access-Control-Allow-Methods",
                          "GET, POST, PUT, DELETE, PATCH, OPTIONS");
  MHD_add_response_header(response, "Access-Control-Allow-Headers",
                          "Content-Type");

  enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;
}
