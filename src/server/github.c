#include <curl/curl.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include <string.h>
#include <ctype.h>
#include "github.h"
#include "auth.h"

// Helper function for CURL write callback
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct {
        Arena *arena;
        char *buffer;
        size_t size;
        size_t capacity;
    } *resp = userdata;
    
    size_t realsize = size * nmemb;
    
    // If we need more space
    if (resp->size + realsize + 1 > resp->capacity) {
        size_t new_capacity = resp->capacity * 2;
        char *new_buffer = arenaAlloc(resp->arena, new_capacity);
        memcpy(new_buffer, resp->buffer, resp->size);
        resp->buffer = new_buffer;
        resp->capacity = new_capacity;
    }
    
    // Append data to buffer
    memcpy(resp->buffer + resp->size, ptr, realsize);
    resp->size += realsize;
    resp->buffer[resp->size] = '\0';
    
    return realsize;
}

enum MHD_Result handleGithubAuthRequest(ServerContext *ctx, struct MHD_Connection *connection) {
    // Get GitHub OAuth configuration
    if (!ctx->website || !ctx->website->auth || !ctx->website->auth->github) {
        return redirectWithError(connection, "/login", "github-not-configured");
    }
    
    const char *clientId = resolveString(ctx->arena, &ctx->website->auth->github->clientId);
    if (!clientId) {
        return redirectWithError(connection, "/login", "github-not-configured");
    }
    
    // Get returnTo from anonymous session if it exists
    char *returnTo = NULL;
    const char *anonymous_session = MHD_lookup_connection_value(connection, MHD_COOKIE_KIND, "anonymous_session");
    if (anonymous_session) {
        const char *valuesSession[] = {anonymous_session};
        PGresult *resultSession = executeParameterizedQuery(ctx->db,
            "SELECT return_path FROM anonymous_sessions WHERE token = $1",
            valuesSession, 1);
        if (resultSession) {
            if (PQntuples(resultSession) > 0) {
                returnTo = strdup(PQgetvalue(resultSession, 0, 0));
            }
            PQclear(resultSession);
        }
    }
    
    // Generate state token for CSRF protection
    char *state = generateToken(ctx->arena);
    if (!state) {
        if (returnTo) {
            free(returnTo);
        }
        return redirectWithError(connection, "/login", "server-error");
    }
    
    // Store state token with returnTo data if present
    json_t *data = json_object();  // Always create an object
    if (returnTo) {
        json_object_set_new(data, "returnTo", json_string(returnTo));
        free(returnTo);
    }
    
    if (!storeStateToken(state, data, ctx)) {
        json_decref(data);  // Always clean up
        return redirectWithError(connection, "/login", "server-error");
    }
    
    json_decref(data);  // Always clean up
    
    // Build GitHub authorization URL
    char redirect_url[1024];
    size_t base_url_len = strlen("https://github.com/login/oauth/authorize?client_id=&state=&scope=user:email");
    size_t client_id_len = clientId ? strlen(clientId) : 0;
    size_t state_len = state ? strlen(state) : 0;

    if (base_url_len + client_id_len + state_len >= sizeof(redirect_url)) {
        fprintf(stderr, "Client ID or state too long for GitHub URL buffer\n");
        return redirectWithError(connection, "/login", "server-error");
    }

    snprintf(redirect_url, sizeof(redirect_url),
             "https://github.com/login/oauth/authorize"
             "?client_id=%s"
             "&state=%s"
             "&scope=user:email",
             clientId, state);
    
    // Create redirect response
    struct MHD_Response *response = 
        MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", redirect_url);
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
enum MHD_Result handleGithubCallback(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    (void)post; // Unused for now since GitHub sends data as GET params
    
    // Get the code and state from query parameters
    const char *code = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "code");
    const char *state = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "state");
    
    // Validate required parameters
    if (!code || !state) {
        printf("Error: Missing code or state\n");
        return redirectWithError(connection, "/login", "github-invalid-response");
    }
    
    // Validate state length to prevent DoS
    size_t state_len = strlen(state);
    if (state_len < 10 || state_len > 128) {
        printf("Error: Invalid state token length\n");
        return redirectWithError(connection, "/login", "github-invalid-state");
    }
    
    // Validate that state contains only hex characters
    for (size_t i = 0; i < state_len; i++) {
        if (!isxdigit(state[i])) {
            printf("Error: Invalid character in state token\n");
            return redirectWithError(connection, "/login", "github-invalid-state");
        }
    }
    
    // Validate state token and get stored data
    json_t *stateData = validateStateToken(state, ctx);
    if (!stateData) {
        printf("Error: Invalid state token\n");
        return redirectWithError(connection, "/login", "github-invalid-state");
    }
    
    // Extract returnTo path if present
    const char *returnTo = NULL;
    json_t *returnToJson = json_object_get(stateData, "returnTo");
    if (returnToJson && json_is_string(returnToJson)) {
        returnTo = json_string_value(returnToJson);
    } else {
        printf("No returnTo found in state data\n");
    }
    
    // Get GitHub OAuth configuration
    if (!ctx->website || !ctx->website->auth || !ctx->website->auth->github) {
        printf("Error: GitHub OAuth not configured\n");
        return redirectWithError(connection, "/login", "github-not-configured");
    }
    
    const char *clientId = resolveString(ctx->arena, &ctx->website->auth->github->clientId);
    const char *clientSecret = resolveString(ctx->arena, &ctx->website->auth->github->clientSecret);
    if (!clientId || !clientSecret) {
        printf("Error: Missing client ID or secret\n");
        return redirectWithError(connection, "/login", "github-not-configured");
    }
    
    // Initialize CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("Error: Failed to initialize CURL\n");
        return redirectWithError(connection, "/login", "server-error");
    }
    
    // Exchange code for access token
    char post_fields[1024];
    snprintf(post_fields, sizeof(post_fields),
             "client_id=%s&client_secret=%s&code=%s",
             clientId, clientSecret, code);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    
    // Initialize response data with arena
    struct {
        Arena *arena;
        char *buffer;
        size_t size;
        size_t capacity;
    } token_resp = {
        .arena = ctx->arena,
        .buffer = arenaAlloc(ctx->arena, 4096),
        .size = 0,
        .capacity = 4096
    };
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://github.com/login/oauth/access_token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &token_resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        printf("Error: Failed to exchange code for token: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return redirectWithError(connection, "/login", "github-token-error");
    }
    
    // Parse access token response
    json_error_t error;
    json_t *token_response = json_loads(token_resp.buffer, 0, &error);
    if (!token_response) {
        printf("Error: Failed to parse token response: %s\n", error.text);
        curl_easy_cleanup(curl);
        return redirectWithError(connection, "/login", "github-token-error");
    }
    
    const char *access_token = json_string_value(json_object_get(token_response, "access_token"));
    if (!access_token) {
        printf("Error: No access token in response\n");
        json_decref(token_response);
        curl_easy_cleanup(curl);
        return redirectWithError(connection, "/login", "github-token-error");
    }
    
    // Get user info from GitHub
    headers = curl_slist_append(NULL, "Accept: application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", access_token);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "User-Agent: WebDSL-App");
    
    // Initialize response data with arena
    struct {
        Arena *arena;
        char *buffer;
        size_t size;
        size_t capacity;
    } user_resp = {
        .arena = ctx->arena,
        .buffer = arenaAlloc(ctx->arena, 4096),
        .size = 0,
        .capacity = 4096
    };
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/user");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &user_resp);
    
    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        printf("Error: Failed to fetch user info: %s\n", curl_easy_strerror(res));
        json_decref(token_response);
        return redirectWithError(connection, "/login", "github-api-error");
    }

    
    // Parse user info
    json_t *user_info = json_loads(user_resp.buffer, 0, &error);
    if (!user_info) {
        printf("Error: Failed to parse user info: %s\n", error.text);
        json_decref(token_response);
        return redirectWithError(connection, "/login", "github-api-error");
    }
    
    // Get user ID and login, converting ID to string if needed
    const char *login = json_string_value(json_object_get(user_info, "login"));
    json_t *id_json = json_object_get(user_info, "id");
    char github_id_str[32];
    const char *github_id = NULL;
    
    if (json_is_integer(id_json)) {
        snprintf(github_id_str, sizeof(github_id_str), "%lld", json_integer_value(id_json));
        github_id = github_id_str;
    }
    
    if (!github_id || !login) {
        printf("Error: Missing user ID or login in GitHub response\n");
        json_decref(token_response);
        json_decref(user_info);
        return redirectWithError(connection, "/login", "github-api-error");
    }
    
    // Check if user already exists
    const char *values[] = {"github", github_id};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT u.id, u.login "
        "FROM users u "
        "JOIN oauth_connections o ON u.id = o.user_id "
        "WHERE o.provider = $1 AND o.provider_user_id = $2 "
        "AND u.status = 'active'",
        values, 2);
    
    char *userId = NULL;
    
    if (result && PQntuples(result) > 0) {
        // Existing user - get their ID
        userId = arenaDupString(ctx->arena, PQgetvalue(result, 0, 0));
        PQclear(result);
        
        // Update OAuth credentials
        json_t *creds = json_object();
        json_object_set_new(creds, "access_token", json_string(access_token));
        json_object_set_new(creds, "updated_at", json_string("NOW()"));
        storeOAuthCredentials(ctx, userId, "github", github_id, access_token, creds);
        json_decref(creds);
    } else {
        if (result) PQclear(result);
        printf("Creating new user account...\n");
        
        // Create new user
        const char *userValues[] = {login, "", NULL, "oauth", "active"};
        result = executeParameterizedQuery(ctx->db,
            "INSERT INTO users (login, password_hash, email, type, status) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING id",
            userValues, 5);
        
        if (!result || PQntuples(result) == 0) {
            printf("Error: Failed to create user account\n");
            json_decref(token_response);
            json_decref(user_info);
            if (result) PQclear(result);
            return redirectWithError(connection, "/login", "server-error");
        }
        
        userId = arenaDupString(ctx->arena, PQgetvalue(result, 0, 0));
        printf("Created new user with ID: %s\n", userId);
        PQclear(result);
        
        // Store OAuth credentials
        json_t *creds = json_object();
        json_object_set_new(creds, "access_token", json_string(access_token));
        json_object_set_new(creds, "user_info", user_info);  // Store full GitHub profile
        if (!storeOAuthCredentials(ctx, userId, "github", github_id, access_token, creds)) {
            printf("Error: Failed to store OAuth credentials\n");
        }
        json_decref(creds);
    }
    
    json_decref(token_response);
    json_decref(user_info);
    
    if (!userId) {
        printf("Error: No user ID after account creation/lookup\n");
        return redirectWithError(connection, "/login", "server-error");
    }
    
    // Create session
    char *sessionToken = createSession(ctx, ctx->arena, userId);
    if (!sessionToken) {
        printf("Error: Failed to create session\n");
        return redirectWithError(connection, "/login", "server-error");
    }
    
    // Create success response with session cookie
    struct MHD_Response *response = 
        MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    char cookie[512];
    snprintf(cookie, sizeof(cookie), 
             "session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400",
             sessionToken);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Use returnTo path if present, otherwise redirect to home
    const char *redirectPath = "/";
    if (returnTo) {
        CURL *curlInit = curl_easy_init();
        if (curlInit) {
            int decodedLength = 0;
            char *decodedPath = curl_easy_unescape(curlInit, returnTo, 0, &decodedLength);
            if (decodedPath) {
                redirectPath = arenaDupString(ctx->arena, decodedPath);
                curl_free(decodedPath);
            }
            curl_easy_cleanup(curlInit);
        }
    }
    
    MHD_add_response_header(response, "Location", redirectPath);
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    if (stateData) json_decref(stateData);
    return ret;
}
#pragma clang diagnostic pop
