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
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop

// Constants
#define INITIAL_FILES_CAPACITY 4
#define MAX_FIELD_NAME_LENGTH 64
#define MAX_FILENAME_LENGTH 255
#define MAX_CONTENT_TYPE_LENGTH 128
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10MB
#define MAX_FIELD_SIZE 8192
#define MAX_FORM_FIELDS 32

// Thread-local storage definition
_Thread_local Arena* currentJsonArena = NULL;

// =============================================================================
// File Upload Management
// =============================================================================

static char* createTempFile(Arena *arena) {
    char template[] = "/tmp/webdsl_upload_XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) {
        fprintf(stderr, "Failed to create temp file: %s\n", strerror(errno));
        return NULL;
    }
    close(fd);
    return arenaDupString(arena, template);
}

static bool initFileUploads(struct PostContext *post) {
    post->files = arenaAlloc(post->arena, INITIAL_FILES_CAPACITY * sizeof(struct FileUpload));
    if (!post->files) return false;
    post->file_capacity = INITIAL_FILES_CAPACITY;
    post->file_count = 0;
    return true;
}

static struct FileUpload* addFileUpload(struct PostContext *post, const char *fieldname) {
    if (!post || !fieldname) {
        return NULL;
    }
    
    // Expand capacity if needed
    if (post->file_count >= post->file_capacity) {
        size_t new_capacity = post->file_capacity * 2;
        struct FileUpload *new_files = arenaAlloc(post->arena, new_capacity * sizeof(struct FileUpload));
        if (!new_files) return NULL;
        
        memcpy(new_files, post->files, post->file_count * sizeof(struct FileUpload));
        post->files = new_files;
        post->file_capacity = new_capacity;
    }
    
    // Initialize new file upload
    struct FileUpload *file = &post->files[post->file_count++];
    memset(file, 0, sizeof(struct FileUpload));
    
    file->fieldname = arenaDupString(post->arena, fieldname);
    if (!file->fieldname) {
        post->file_count--;
        return NULL;
    }
    
    file->tempPath = createTempFile(post->arena);
    if (!file->tempPath) {
        post->file_count--;
        return NULL;
    }
    
    file->fp = fopen(file->tempPath, "wb");
    if (!file->fp) {
        post->file_count--;
        return NULL;
    }
    
    file->max_size = MAX_FILE_SIZE;
    return file;
}

static void cleanupFileUploads(struct PostContext *post) {
    if (!post || !post->files) return;
    
    for (size_t i = 0; i < post->file_count; i++) {
        if (post->files[i].fp) {
            fclose(post->files[i].fp);
            post->files[i].fp = NULL;
        }
        if (post->files[i].tempPath) {
            unlink(post->files[i].tempPath);
        }
    }
}

// =============================================================================
// Input Validation
// =============================================================================

static bool validateFieldName(const char *key) {
    return key && strlen(key) <= MAX_FIELD_NAME_LENGTH;
}

static bool validateFilename(const char *filename) {
    if (!filename || strlen(filename) > MAX_FILENAME_LENGTH) {
        return false;
    }
    
    // Check for directory traversal attempts
    if (strstr(filename, "..") || strchr(filename, '/') || strchr(filename, '\\')) {
        return false;
    }
    
    return true;
}

static bool validateContentType(const char *content_type) {
    return !content_type || strlen(content_type) <= MAX_CONTENT_TYPE_LENGTH;
}

static bool validateFileSize(size_t current_size, size_t new_data_size, size_t max_size) {
    return (current_size + new_data_size) <= max_size;
}

static bool validateFieldSize(size_t size) {
    return size <= MAX_FIELD_SIZE;
}

// =============================================================================
// Form Data Processing
// =============================================================================

static struct FileUpload* findFileUpload(struct PostContext *post, const char *key) {
    for (size_t i = 0; i < post->file_count; i++) {
        if (strcmp(post->files[i].fieldname, key) == 0) {
            return &post->files[i];
        }
    }
    return NULL;
}

static enum MHD_Result processFileUpload(struct PostContext *post, const char *key,
                                       const char *filename, const char *content_type,
                                       const char *data, size_t size) {
    struct FileUpload *file = findFileUpload(post, key);
    
    if (!file) {
        // New file upload
        file = addFileUpload(post, key);
        if (!file) return MHD_NO;
        
        // Store file metadata
        file->filename = arenaDupString(post->arena, filename);
        file->mimetype = content_type ? arenaDupString(post->arena, content_type) : NULL;
    }
    
    // Check file size limit
    if (!validateFileSize(file->size, size, file->max_size)) {
        fprintf(stderr, "File upload exceeds maximum allowed size\n");
        return MHD_NO;
    }
    
    // Write data to temp file
    if (fwrite(data, 1, size, file->fp) != size) {
        return MHD_NO;
    }
    
    file->size += size;
    return MHD_YES;
}

static enum MHD_Result processFormField(struct PostContext *post, const char *key,
                                      const char *data, uint64_t off, size_t size) {
    if (off == 0) {
        // Validate field size
        if (!validateFieldSize(size)) {
            fprintf(stderr, "Form field value too large\n");
            return MHD_NO;
        }
        
        // Store field value
        if (post->post_data.value_count < MAX_FORM_FIELDS) {
            post->post_data.values[post->post_data.value_count] = arenaDupString(post->arena, data);
            post->post_data.keys[post->post_data.value_count] = arenaDupString(post->arena, key);
            post->post_data.value_count++;
        }
    }
    return MHD_YES;
}

static enum MHD_Result handleMultipartData(void *coninfo_cls, 
                                         enum MHD_ValueKind kind,
                                         const char *key,
                                         const char *filename,
                                         const char *content_type,
                                         const char *transfer_encoding,
                                         const char *data, 
                                         uint64_t off, 
                                         size_t size) {
    (void)kind; (void)transfer_encoding;
    
    struct PostContext *post = coninfo_cls;
    if (!post || !key) return MHD_NO;
    
    // Validate input parameters
    if (!validateFieldName(key)) {
        fprintf(stderr, "Field name too long\n");
        return MHD_NO;
    }
    
    if (filename) {
        if (!validateFilename(filename)) {
            fprintf(stderr, "Invalid filename containing path traversal characters\n");
            return MHD_NO;
        }
        
        if (!validateContentType(content_type)) {
            fprintf(stderr, "Content type too long\n");
            return MHD_NO;
        }
        
        return processFileUpload(post, key, filename, content_type, data, size);
    } else {
        return processFormField(post, key, data, off, size);
    }
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
    if (post_data->value_count < MAX_FORM_FIELDS) {
        post_data->values[post_data->value_count] = arenaDupString(post_data->arena, data);
        post_data->keys[post_data->value_count] = arenaDupString(post_data->arena, key);
        post_data->value_count++;
    }
    
    return MHD_YES;
}

// =============================================================================
// JSON Processing
// =============================================================================

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

// =============================================================================
// Context Management
// =============================================================================

static enum RequestType determineRequestType(const char *content_type) {
    if (!content_type) {
        return REQUEST_TYPE_POST;
    }
    
    if (strstr(content_type, "application/json")) {
        return REQUEST_TYPE_JSON_POST;
    } else if (strstr(content_type, "multipart/form-data")) {
        return REQUEST_TYPE_MULTIPART;
    } else {
        return REQUEST_TYPE_POST;
    }
}

static struct PostContext* initializePostContext(Arena *arena, struct MHD_Connection *connection) {
    struct PostContext *post = arenaAlloc(arena, sizeof(struct PostContext));
    if (!post) return NULL;
    
    memset(post, 0, sizeof(struct PostContext));
    post->arena = arena;
    post->post_data.arena = arena;
    post->post_data.connection = connection;
    
    return initFileUploads(post) ? post : NULL;
}

static enum MHD_Result setupPostProcessor(struct PostContext *post, struct MHD_Connection *connection, Arena *arena) {
    const char *content_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
    
    post->type = determineRequestType(content_type);
    
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (post->type) {
        case REQUEST_TYPE_JSON_POST:
            // No post processor needed for JSON
            break;
            
        case REQUEST_TYPE_MULTIPART:
            if (!initFileUploads(post)) {
                return MHD_NO;
            }
            post->pp = MHD_create_post_processor(connection, 64 * 1024, handleMultipartData, post);
            break;
            
        default:
            post->pp = MHD_create_post_processor(connection, 32 * 1024, post_iterator, &post->post_data);
            break;
    }
    #pragma clang diagnostic pop
    
    if (post->type != REQUEST_TYPE_JSON_POST && !post->pp) {
        freeArena(arena);
        return MHD_NO;
    }
    
    return MHD_YES;
}

static struct RequestContext* initializeGetContext(Arena *arena) {
    struct RequestContext *reqctx = arenaAlloc(arena, sizeof(struct RequestContext));
    if (!reqctx) return NULL;
    
    reqctx->arena = arena;
    reqctx->type = REQUEST_TYPE_GET;
    return reqctx;
}

// =============================================================================
// Request Context Building
// =============================================================================

static enum MHD_Result jsonKvIterator(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
    (void)kind; // Suppress unused parameter warning
    json_t *obj = (json_t *)cls;
    json_object_set_new(obj, key, json_string(value));
    return MHD_YES;
}

static json_t* buildUserContext(ServerContext *ctx, struct MHD_Connection *connection) {
    json_t *user = getUser(ctx, connection);
    return user ? user : json_null();
}

static json_t* buildQueryParams(struct MHD_Connection *connection) {
    json_t *query = json_object();
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, jsonKvIterator, query);
    return query;
}

static json_t* buildHeaders(struct MHD_Connection *connection) {
    json_t *headers = json_object();
    MHD_get_connection_values(connection, MHD_HEADER_KIND, jsonKvIterator, headers);
    return headers;
}

static json_t* buildCookies(struct MHD_Connection *connection) {
    json_t *cookies = json_object();
    MHD_get_connection_values(connection, MHD_COOKIE_KIND, jsonKvIterator, cookies);
    return cookies;
}

static json_t* buildParams(RouteParams *params) {
    json_t *params_obj = json_object();
    for (int i = 0; i < params->count; i++) {
        json_object_set_new(params_obj, params->params[i].name, json_string(params->params[i].value));
    }
    return params_obj;
}

static json_t* buildBodyFromFormData(struct PostContext *post_ctx) {
    json_t *body = json_object();
    for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
        const char *value = post_ctx->post_data.values[i];
        const char *key = post_ctx->post_data.keys[i];
        if (value) {
            json_object_set_new(body, key, json_string(value));
        }
    }
    return body;
}

static json_t* buildBodyFromJson(struct PostContext *post_ctx) {
    if (!post_ctx->raw_json) {
        return json_object();
    }
    
    json_error_t error;
    json_t *json_body = json_loads(post_ctx->raw_json, 0, &error);
    return json_body ? json_body : json_object();
}

static json_t* buildBody(const char *method, void *con_cls) {
    if (strcmp(method, "POST") != 0) {
        return json_object();
    }
    
    struct PostContext *post_ctx = con_cls;
    if (!post_ctx) {
        return json_object();
    }
    
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (post_ctx->type) {
        case REQUEST_TYPE_JSON_POST:
            return buildBodyFromJson(post_ctx);
        case REQUEST_TYPE_POST:
            return buildBodyFromFormData(post_ctx);
        default:
            return json_object();
    }
    #pragma clang diagnostic pop
}

static json_t* buildFiles(const char *method, void *con_cls) {
    json_t *files = json_object();
    
    if (strcmp(method, "POST") != 0) {
        return files;
    }
    
    struct PostContext *post_ctx = con_cls;
    if (!post_ctx || post_ctx->type != REQUEST_TYPE_MULTIPART) {
        return files;
    }
    
    for (size_t i = 0; i < post_ctx->file_count; i++) {
        struct FileUpload *file = &post_ctx->files[i];
        if (file->fp) {
            fclose(file->fp);  // Close file handle after upload
            file->fp = NULL;
        }
        
        json_t *file_obj = json_object();
        json_object_set_new(file_obj, "filename", json_string(file->filename));
        json_object_set_new(file_obj, "mimetype", file->mimetype ? json_string(file->mimetype) : json_null());
        json_object_set_new(file_obj, "size", json_integer((json_int_t)file->size));
        json_object_set_new(file_obj, "tempPath", json_string(file->tempPath));
        
        json_object_set_new(files, file->fieldname, file_obj);
    }
    
    return files;
}

static json_t* buildRequestContextJson(ServerContext *ctx, struct MHD_Connection *connection, Arena *arena, 
                                   void *con_cls, const char *method, 
                                   const char *url, const char *version,
                                   RouteParams *params) {
    (void)arena;
    json_t *context = json_object();

    // Basic request info
    json_object_set_new(context, "method", json_string(method));
    json_object_set_new(context, "url", json_string(url));
    json_object_set_new(context, "version", json_string(version));
    
    // User authentication
    json_t *user = buildUserContext(ctx, connection);
    if (user && !json_is_null(user)) {
        json_object_set_new(context, "user", user);
        json_object_set_new(context, "isLoggedIn", json_true());
    } else {
        json_object_set_new(context, "isLoggedIn", json_false());
    }
    
    // Request components
    json_object_set_new(context, "query", buildQueryParams(connection));
    json_object_set_new(context, "headers", buildHeaders(connection));
    json_object_set_new(context, "cookies", buildCookies(connection));
    json_object_set_new(context, "params", buildParams(params));
    json_object_set_new(context, "body", buildBody(method, con_cls));
    json_object_set_new(context, "files", buildFiles(method, con_cls));
    
    return context;
}

// =============================================================================
// Request Routing
// =============================================================================

static bool isBodyMethod(const char *method) {
    return strcmp(method, "POST") == 0 || 
           strcmp(method, "PUT") == 0 || 
           strcmp(method, "PATCH") == 0;
}

static enum MHD_Result handleAuthEndpoints(ServerContext *ctx, struct MHD_Connection *connection, 
                                         const char *url, struct PostContext *post) {
    if (strcmp(url, "/login") == 0) {
        return handleLoginRequest(ctx, connection, post);
    }
    if (strcmp(url, "/logout") == 0) {
        return handleLogoutRequest(ctx, connection);
    }
    if (strcmp(url, "/register") == 0) {
        return handleRegisterRequest(ctx, connection, post);
    }
    if (strcmp(url, "/resend-verification") == 0) {
        return handleResendVerificationRequest(ctx, connection);
    }
    if (strcmp(url, "/forgot-password") == 0) {
        return handleForgotPasswordRequest(ctx, connection, post);
    }
    if (strcmp(url, "/reset-password") == 0) {
        return handleResetPasswordRequest(ctx, connection, post);
    }
    return MHD_NO; // Not an auth endpoint
}

static enum MHD_Result handleGetAuthEndpoints(ServerContext *ctx, struct MHD_Connection *connection, 
                                            const char *url) {
    if (strcmp(url, "/verify-email") == 0) {
        const char *token = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "token");
        if (token) {
            return handleVerifyEmailRequest(ctx, connection, token);
        }
    }
    if (strcmp(url, "/auth/github") == 0) {
        return handleGithubAuthRequest(ctx, connection);
    }
    if (strcmp(url, "/auth/github/callback") == 0) {
        return handleGithubCallback(ctx, connection, NULL);
    }
    return MHD_NO; // Not an auth endpoint
}

static enum MHD_Result handleSpecialEndpoints(ServerContext *ctx, struct MHD_Connection *connection, 
                                            const char *url, const char *method, 
                                            Arena *requestArena, void *con_cls) {
    // Handle CSS endpoint
    if (strcmp(url, "/styles.css") == 0) {
        return handleCssRequest(connection, requestArena);
    }
    
    // Handle auth endpoints
    if (isBodyMethod(method)) {
        struct PostContext *post = con_cls;
        enum MHD_Result result = handleAuthEndpoints(ctx, connection, url, post);
        if (result != MHD_NO) return result;
    } else {
        enum MHD_Result result = handleGetAuthEndpoints(ctx, connection, url);
        if (result != MHD_NO) return result;
    }
    
    return MHD_NO; // Not a special endpoint
}

static enum MHD_Result handleValidationErrors(struct MHD_Connection *connection, 
                                            json_t *validation_errors) {
    if (!validation_errors) return MHD_YES;
    
    char *error_str = json_dumps(validation_errors, 0);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(error_str), error_str, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
    MHD_destroy_response(response);
    return ret;
}

static json_t* executeRouteValidation(ServerContext *ctx, RouteMatch *match, 
                                    struct PostContext *post, json_t *requestContext, 
                                    Arena *requestArena) {
    if (match->type == ROUTE_TYPE_API && match->endpoint.api->apiFields) {
        if (post->type == REQUEST_TYPE_JSON_POST) {
            return validateJsonFields(requestArena, match->endpoint.api->apiFields, post);
        }
    } else if (match->type == ROUTE_TYPE_PAGE && match->endpoint.page->fields) {
        if (post->type == REQUEST_TYPE_POST) {
            // Execute reference data before validation if it exists
            if (match->endpoint.page->referenceData) {
                json_t *refData = executePipeline(ctx, match->endpoint.page->referenceData, requestContext, requestArena);
                if (refData) {
                    json_object_update(requestContext, refData);
                }
            }
            return validateFormFields(requestArena, match->endpoint.page->fields, post);
        }
    }
    return NULL;
}

static json_t* executePipelineIfExists(ServerContext *ctx, RouteMatch *match, 
                                     json_t *requestContext, Arena *requestArena) {
    PipelineStepNode *pipeline = NULL;
    
    // Get pipeline from either API or page
    if (match->type == ROUTE_TYPE_API && match->endpoint.api->uses_pipeline) {
        pipeline = match->endpoint.api->pipeline;
    } else if (match->type == ROUTE_TYPE_PAGE && match->endpoint.page->pipeline) {
        pipeline = match->endpoint.page->pipeline;
    }
    
    if (pipeline) {
        return executePipeline(ctx, pipeline, requestContext, requestArena);
    } else {
        json_t *context = json_object();
        json_object_set_new(context, "request", json_deep_copy(requestContext));
        return context;
    }
}

static enum MHD_Result handlePipelineRedirect(struct MHD_Connection *connection, json_t *pipelineResult) {
    json_t *redirect = json_object_get(pipelineResult, "redirect");
    if (!redirect) return MHD_YES;
    
    const char *redirectPath = json_string_value(redirect);
    if (!redirectPath) return MHD_YES;
    
    struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", redirectPath);
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result handleRouteResponse(struct MHD_Connection *connection, RouteMatch *match, 
                                         const char *method, json_t *pipelineResult, 
                                         json_t *requestContext, Arena *requestArena) {
    switch (match->type) {
        case ROUTE_TYPE_API:
            return handleApiRequest(connection, match->endpoint.api, method, pipelineResult);

        case ROUTE_TYPE_PAGE:
            // Add requestContext to pipelineResult
            json_object_set_new(pipelineResult, "request", json_deep_copy(requestContext));
            return handlePageRequest(connection, match->endpoint.page, requestArena, pipelineResult);

        case ROUTE_TYPE_NONE:
            break;
    }

    // Return 404 for unmatched routes
    char *not_found = "<html><body><h1>404 Not Found</h1></body></html>";
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(not_found), not_found, MHD_RESPMEM_PERSISTENT);
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

// =============================================================================
// Main Request Handler
// =============================================================================

enum MHD_Result handleRequest(ServerContext *ctx,
                            struct MHD_Connection *connection,
                            const char *url,
                            const char *method,
                            const char *version,
                            const char *upload_data,
                            size_t *upload_data_size,
                            void **con_cls) {
    (void)version; (void)ctx;

    // First call for this connection
    if (*con_cls == NULL) {
        Arena *arena = createArena(1024 * 1024); // 1MB initial size
        
        if (isBodyMethod(method)) {
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
    if (isBodyMethod(method)) {
        struct PostContext *post = *con_cls;
        requestArena = post->arena;
    } else {
        struct RequestContext *reqctx = *con_cls;
        requestArena = reqctx->arena;
    }

    // Initialize JSON arena for this request
    initRequestJsonArena(requestArena);

    // Handle request body data
    if (isBodyMethod(method)) {
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
    }

    // Handle special endpoints (auth, CSS, etc.)
    enum MHD_Result specialResult = handleSpecialEndpoints(ctx, connection, url, method, requestArena, *con_cls);
    if (specialResult != MHD_NO) {
        return specialResult;
    }

    // Find route using unified routing
    RouteMatch match = findRoute(url, method, requestArena);

    // Build request context once - AFTER all POST data is processed
    json_t *requestContext = buildRequestContextJson(ctx, connection, requestArena, *con_cls, 
                                                   method, url, version, &match.params);

    // Handle validation for requests with body
    if (isBodyMethod(method)) {
        struct PostContext *post = *con_cls;
        json_t *validation_errors = executeRouteValidation(ctx, &match, post, requestContext, requestArena);
        
        if (validation_errors) {
            if (match.type == ROUTE_TYPE_API) {
                return handleValidationErrors(connection, validation_errors);
            } else if (match.type == ROUTE_TYPE_PAGE) {
                json_object_update(requestContext, validation_errors);
                return handlePageRequest(connection, match.endpoint.page, requestArena, requestContext);
            }
        }
    }
    
    // Execute pipeline if exists
    json_t *pipelineResult = executePipelineIfExists(ctx, &match, requestContext, requestArena);
    
    // Handle pipeline redirects
    if (pipelineResult) {
        enum MHD_Result redirectResult = handlePipelineRedirect(connection, pipelineResult);
        if (redirectResult != MHD_YES) {
            return redirectResult;
        }
    }

    // Handle based on route type
    return handleRouteResponse(connection, &match, method, pipelineResult, requestContext, requestArena);
}

// =============================================================================
// Request Cleanup
// =============================================================================

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
        
        if (reqctx->type == REQUEST_TYPE_POST || 
            reqctx->type == REQUEST_TYPE_JSON_POST ||
            reqctx->type == REQUEST_TYPE_MULTIPART) {
            struct PostContext *post = (struct PostContext *)reqctx;
            
            // Clean up file uploads
            cleanupFileUploads(post);
            
            if (post->pp) {
                MHD_destroy_post_processor(post->pp);
            }
            
            freeArena(post->arena);
        } else {
            freeArena(reqctx->arena);
        }
        *con_cls = NULL;
    }
}
