#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#include <argon2.h>
#pragma clang diagnostic pop
#include "server.h"
#include "db.h"
#include "auth.h"
#include "email.h"
#include <microhttpd.h>
#include <jansson.h>
#include <libpq-fe.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>

// Forward declarations of helper functions
static char* createVerificationToken(ServerContext *ctx, Arena *arena, const char *userId);
static char* hashPassword(Arena *arena, const char *password, const char *configSalt);

static void cleanupOldStateTokens(ServerContext *ctx) {
    // Delete tokens older than 10 minutes
    const char *sql = "DELETE FROM state_tokens WHERE created_at < NOW() - INTERVAL '10 minutes'";
    PGresult *result = executeQuery(ctx->db, sql);
    if (result) {
        PQclear(result);
    }
}

json_t* validateStateToken(const char *token, ServerContext *ctx) {
    json_t *data = NULL;
    
    // First cleanup old tokens
    cleanupOldStateTokens(ctx);
    
    // Look for and delete matching token
    const char *values[] = {token};
    const char *sql = "DELETE FROM state_tokens WHERE token = $1 RETURNING data";
    
    PGresult *result = executeParameterizedQuery(ctx->db, sql, values, 1);
    if (result) {
        if (PQntuples(result) > 0 && !PQgetisnull(result, 0, 0)) {
            json_error_t error;
            data = json_loads(PQgetvalue(result, 0, 0), 0, &error);
        }
        PQclear(result);
    }
    
    return data;
}

bool storeStateToken(const char *token, json_t *data, ServerContext *ctx) {
    bool stored = false;
    
    // First cleanup old tokens
    cleanupOldStateTokens(ctx);
    
    // Convert data to JSON string if present
    char *data_str = NULL;
    if (data) {
        data_str = json_dumps(data, JSON_COMPACT);
    }
    
    // Store new token with data
    const char *values[] = {token, data_str ? data_str : NULL};
    const char *sql = "INSERT INTO state_tokens (token, data) VALUES ($1, $2::jsonb)";
    
    PGresult *result = executeParameterizedQuery(ctx->db, sql, values, 2);
    if (result) {
        stored = PQresultStatus(result) == PGRES_COMMAND_OK;
        PQclear(result);
    }
    
    return stored;
}

// Helper function to create a password reset token
static char* createPasswordResetToken(ServerContext *ctx, Arena *arena, const char *userId) {
    char *token = generateToken(arena);
    if (!token) {
        return NULL;
    }

    // Insert token into password_resets table with 1 hour expiration
    const char *values[] = {userId, token};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO password_resets (user_id, token, expires_at) "
        "VALUES ($1, $2, NOW() + INTERVAL '1 hour') RETURNING token",
        values, 2);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        if (result) PQclear(result);
        return NULL;
    }

    PQclear(result);
    return token;
}

bool storeOAuthCredentials(ServerContext *ctx, const char *userId, 
                                const char *provider, const char *providerId,
                                const char *accessToken, json_t *credentials) {
    // Use arena allocation for the JSON string
    size_t json_size = json_dumpb(credentials, NULL, 0, JSON_COMPACT);
    char *creds_str = arenaAlloc(ctx->arena, json_size + 1);
    json_dumpb(credentials, creds_str, json_size + 1, JSON_COMPACT);
    
    const char *values[] = {userId, provider, providerId, accessToken, creds_str};
    
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO oauth_connections "
        "(user_id, provider, provider_user_id, access_token, credentials) "
        "VALUES ($1, $2, $3, $4, $5::jsonb) "
        "ON CONFLICT (provider, provider_user_id) DO UPDATE "
        "SET access_token = $4, credentials = $5::jsonb, updated_at = NOW()",
        values, 5);
        
    bool success = result != NULL;
    if (result) PQclear(result);
    return success;
}

// Get user from session token with enhanced user info
json_t* getUser(ServerContext *ctx, struct MHD_Connection *connection) {
    if (!ctx || !connection || !ctx->db) {
        return NULL;
    }
    
    const char *sessionToken = MHD_lookup_connection_value(connection, 
                                                         MHD_COOKIE_KIND, 
                                                         "session");
    if (!sessionToken) {
        return NULL;
    }

    // Look up valid session and user with enhanced info
    const char *values[] = {sessionToken};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT u.id, u.login, u.email, u.type, u.status, u.created_at, "
        "       o.provider as auth_provider, o.credentials "
        "FROM sessions s "
        "JOIN users u ON u.id = s.user_id "
        "LEFT JOIN oauth_connections o ON u.id = o.user_id "
        "WHERE s.token = $1 "
        "AND s.expires_at > NOW() "
        "AND u.status = 'active'",
        values, 1);

    if (!result || PQntuples(result) == 0) {
        if (result) PQclear(result);
        return NULL;
    }

    // Create enhanced user object
    json_t *user = json_object();
    json_object_set_new(user, "id", json_string(PQgetvalue(result, 0, 0)));
    json_object_set_new(user, "login", json_string(PQgetvalue(result, 0, 1)));
    
    // Handle potentially NULL email
    if (!PQgetisnull(result, 0, 2)) {
        json_object_set_new(user, "email", json_string(PQgetvalue(result, 0, 2)));
    }
    
    json_object_set_new(user, "type", json_string(PQgetvalue(result, 0, 3)));
    json_object_set_new(user, "status", json_string(PQgetvalue(result, 0, 4)));
    json_object_set_new(user, "created_at", json_string(PQgetvalue(result, 0, 5)));
    
    // Handle OAuth info if present
    if (!PQgetisnull(result, 0, 6)) {
        json_object_set_new(user, "auth_provider", json_string(PQgetvalue(result, 0, 6)));
        // Parse and include credentials if needed
        json_error_t error;
        json_t *credentials = json_loads(PQgetvalue(result, 0, 7), 0, &error);
        if (credentials) {
            json_object_set_new(user, "oauth_credentials", credentials);
        }
    }

    PQclear(result);
    return user;
}

static bool verifyPassword(const char *password, const char *storedHash, const char *configSalt) {
    // Validate input parameters
    if (!password || !storedHash || !configSalt) {
        return false;
    }
    
    // Check password length limits
    size_t password_len = strlen(password);
    if (password_len < 8 || password_len > 128) {
        return false;  // Password too short or suspiciously long
    }
    
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // Salt from config
    
    // Convert hex salt string to bytes
    for (size_t i = 0; i < 16 && configSalt[i*2] && configSalt[i*2+1]; i++) {
        char hex[3] = {configSalt[i*2], configSalt[i*2+1], '\0'};
        salt[i] = (uint8_t)strtol(hex, NULL, 16);
    }
    
    int result = argon2id_hash_raw(
        2,      // iterations
        1 << 16, // 64MB memory
        1,      // parallelism
        password, strlen(password),
        salt, sizeof(salt),
        hash, sizeof(hash)
    );
    
    if (result != ARGON2_OK) {
        fprintf(stderr, "Failed to hash password: %s\n", argon2_error_message(result));
        return false;
    }
    
    // Convert hash to hex string for comparison
    char hashStr[65] = {0};  // Initialize to zero
    for (size_t i = 0; i < sizeof(hash); i++) {
        snprintf(&hashStr[i * 2], 3, "%02x", hash[i]);
    }
    hashStr[64] = '\0';
    
    return strcmp(hashStr, storedHash) == 0;
}

char* createSession(ServerContext *ctx, Arena *arena, const char *userId) {
    // Generate a random session token using urandom
    uint8_t random_bytes[32];
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) {
        fprintf(stderr, "Failed to open /dev/urandom\n");
        return NULL;
    }
    
    if (fread(random_bytes, 1, sizeof(random_bytes), urandom) != sizeof(random_bytes)) {
        fprintf(stderr, "Failed to read from /dev/urandom\n");
        fclose(urandom);
        return NULL;
    }
    fclose(urandom);
    
    // Convert random bytes to hex string
    char token[65] = {0};  // Initialize to zero
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        snprintf(&token[i*2], 3, "%02x", random_bytes[i]);
    }
    token[64] = '\0';
    
    // Insert session with expiration time
    const char *values[] = {userId, token};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO sessions (user_id, token, expires_at) "
        "VALUES ($1, $2, NOW() + INTERVAL '24 hours') RETURNING token",
        values, 2);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Failed to create session\n");
        if (result) PQclear(result);
        return NULL;
    }

    // Get the token back and store it in the arena
    char *sessionToken = arenaDupString(arena, PQgetvalue(result, 0, 0));
    PQclear(result);
    
    return sessionToken;
}

enum MHD_Result redirectWithError(struct MHD_Connection *connection,
                                         const char *location,
                                         const char *error_key) {
    // Create redirect URL with error parameter
    char redirect_url[512];
    size_t base_url_len = strlen("http://localhost:8080/");
    size_t location_len = location ? strlen(location) : 0;

    if (base_url_len + location_len >= sizeof(redirect_url)) {
        fprintf(stderr, "Location too long for redirect URL buffer\n");
        return MHD_NO;
    }
    snprintf(redirect_url, sizeof(redirect_url), "%s?error=%s", location, error_key);

    struct MHD_Response *response =
        MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", redirect_url);

    enum MHD_Result ret =
        MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);

    return ret;
}

enum MHD_Result handleLoginRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;
    const char *password = NULL;
    const char *salt = NULL;

    // Get salt from website config
    if (ctx->website && ctx->website->auth) {
        salt = resolveString(post->arena, &ctx->website->auth->salt);
    }
    
    if (!salt) {
        fprintf(stderr, "No salt configured in website auth block\n");
        return redirectWithError(connection, "/login", "server-error");
    }

    // get login and password from post data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "login") == 0) {
            login = value;
        } else if (strcmp(key, "password") == 0) {
            password = value;
        }
    }

    if (!login || !password) {
        return redirectWithError(connection, "/login", "missing-fields");
    }

    // Look up user in database
    const char *values[] = {login};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT id, password_hash FROM users WHERE login = $1",
        values, 1);

    if (!result || PQntuples(result) == 0) {
        if (result) PQclear(result);
        return redirectWithError(connection, "/login", "invalid-credentials");
    }

    // Get stored password hash and user ID
    char *storedHash = arenaDupString(post->arena, PQgetvalue(result, 0, 1));
    char *userId = arenaDupString(post->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Verify password
    if (!verifyPassword(password, storedHash, salt)) {
        return redirectWithError(connection, "/login", "invalid-credentials");
    }
    
    // Create session
    char *token = createSession(ctx, post->arena, userId);
    if (!token) {
        return redirectWithError(connection, "/login", "server-error");
    }
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Set session cookie
    char cookie[256];
    size_t base_cookie_len = strlen("session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
    size_t token_len = token ? strlen(token) : 0;

    if (base_cookie_len + token_len >= sizeof(cookie)) {
        fprintf(stderr, "Token too long for cookie buffer\n");
        return redirectWithError(connection, "/login", "server-error");
    }

    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400", token);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    const char *redirectPath = "/";  // Default path
    
    // Use return path from anonymous session
    const char *anonymous_session = MHD_lookup_connection_value(
        connection, MHD_COOKIE_KIND, "anonymous_session");
    if (anonymous_session) {
        const char *valuesSession[] = {anonymous_session};
        PGresult *resultSession = executeParameterizedQuery(ctx->db,
            "SELECT return_path FROM anonymous_sessions WHERE token = $1",
            valuesSession, 1);
        if (resultSession) {
            if (PQntuples(resultSession) > 0) {
                const char *return_path = PQgetvalue(resultSession, 0, 0);
                if (return_path) {
                    redirectPath = arenaDupString(post->arena, return_path);
                }
            }
            PQclear(resultSession);
        }
    }
    
    printf("redirectPath: %s\n", redirectPath);
    
    // Set redirect header
    MHD_add_response_header(response, "Location", redirectPath);
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

enum MHD_Result handleLogoutRequest(ServerContext *ctx, struct MHD_Connection *connection) {
    // Get session cookie
    const char *cookie = MHD_lookup_connection_value(connection, MHD_COOKIE_KIND, "session");
    if (cookie) {
        // Delete session from database
        const char *values[] = {cookie};
        PGresult *result = executeParameterizedQuery(ctx->db,
            "DELETE FROM sessions WHERE token = $1",
            values, 1);
        if (result) {
            PQclear(result);
        }
    }
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Clear session cookie by setting it to empty with immediate expiry
    MHD_add_response_header(response, "Set-Cookie", 
                          "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    
    // Redirect to home page
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

static char* hashPassword(Arena *arena, const char *password, const char *configSalt) {
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // Salt from config
    
    // Convert hex salt string to bytes
    for (size_t i = 0; i < 16 && configSalt[i*2] && configSalt[i*2+1]; i++) {
        char hex[3] = {configSalt[i*2], configSalt[i*2+1], '\0'};
        salt[i] = (uint8_t)strtol(hex, NULL, 16);
    }
    
    int result = argon2id_hash_raw(
        2,      // iterations
        1 << 16, // 64MB memory
        1,      // parallelism
        password, strlen(password),
        salt, sizeof(salt),
        hash, sizeof(hash)
    );
    
    if (result != ARGON2_OK) {
        fprintf(stderr, "Failed to hash password: %s\n", argon2_error_message(result));
        return NULL;
    }
    
    // Convert hash to hex string
    char *hashStr = arenaAlloc(arena, sizeof(hash) * 2 + 1);
    for (size_t i = 0; i < sizeof(hash); i++) {
        snprintf(&hashStr[i * 2], 3, "%02x", hash[i]);
    }
    hashStr[64] = '\0';
    
    return hashStr;
}

enum MHD_Result handleRegisterRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;
    const char *password = NULL;
    const char *confirm_password = NULL;
    const char *salt = NULL;

    // Get salt from website config
    if (ctx->website && ctx->website->auth) {
        salt = resolveString(post->arena, &ctx->website->auth->salt);
    }
    
    if (!salt) {
        fprintf(stderr, "No salt configured in website auth block\n");
        return redirectWithError(connection, "/register", "server-error");
    }

    // Get form data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "login") == 0) {
            login = value;
        } else if (strcmp(key, "password") == 0) {
            password = value;
        } else if (strcmp(key, "confirm_password") == 0) {
            confirm_password = value;
        }
    }

    // Basic validation
    if (!login || !password || !confirm_password) {
        return redirectWithError(connection, "/register", "missing-fields");
    }
    
    if (strcmp(password, confirm_password) != 0) {
        return redirectWithError(connection, "/register", "password-mismatch");
    }

    // Check if user already exists
    const char *values[] = {login};
    PGresult *checkResult = executeParameterizedQuery(ctx->db,
        "SELECT id FROM users WHERE login = $1 OR email = $1",
        values, 1);

    if (!checkResult) {
        return redirectWithError(connection, "/register", "server-error");
    }

    if (PQntuples(checkResult) > 0) {
        PQclear(checkResult);
        return redirectWithError(connection, "/register", "email-taken");
    }
    PQclear(checkResult);

    // Hash password
    char *passwordHash = hashPassword(post->arena, password, salt);
    if (!passwordHash) {
        return redirectWithError(connection, "/register", "server-error");
    }

    // Insert user into database
    const char *insertValues[] = {login, passwordHash, login, "local", "active"};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO users (login, password_hash, email, type, status) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id",
        insertValues, 5);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Failed to create user account\n");
        if (result) PQclear(result);
        return redirectWithError(connection, "/register", "server-error");
    }

    // Get the new user's ID
    char *userId = arenaDupString(post->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Create verification token
    char *token = createVerificationToken(ctx, post->arena, userId);
    if (!token) {
        return redirectWithError(connection, "/register", "server-error");
    }

    // Build verification URL
    char verificationUrl[512];
    size_t base_url_len = strlen("http://localhost:8080/verify-email?token=");
    size_t token_len = token ? strlen(token) : 0;

    if (base_url_len + token_len >= sizeof(verificationUrl)) {
        fprintf(stderr, "Token too long for verification URL buffer\n");
        return redirectWithError(connection, "/verify-email", "server-error");
    }

    snprintf(verificationUrl, sizeof(verificationUrl), "http://localhost:8080/verify-email?token=%s", token);

    // Send verification email using new email service
    if (sendVerificationEmail(ctx, post->arena, login, verificationUrl) < 0) {
        return redirectWithError(connection, "/register", "server-error");
    }

    // Create session
    char *sessionToken = createSession(ctx, post->arena, userId);
    if (!sessionToken) {
        return redirectWithError(connection, "/register", "server-error");
    }
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Set session cookie
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400", sessionToken);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Redirect to verify email page
    MHD_add_response_header(response, "Location", "/verify-email");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Helper function to generate a random token
char* generateToken(Arena *arena) {
    uint8_t random_bytes[32];
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) {
        fprintf(stderr, "Failed to open /dev/urandom\n");
        return NULL;
    }
    
    // Check that we read the expected number of bytes
    size_t bytes_read = fread(random_bytes, 1, sizeof(random_bytes), urandom);
    if (bytes_read != sizeof(random_bytes)) {
        fprintf(stderr, "Failed to read enough random bytes: %zu/%zu\n", 
                bytes_read, sizeof(random_bytes));
        fclose(urandom);
        return NULL;
    }
    fclose(urandom);
    
    // Convert random bytes to hex string
    char *token = arenaAlloc(arena, 65);
    if (!token) {
        return NULL;
    }
    
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        snprintf(&token[i*2], 3, "%02x", random_bytes[i]);
    }
    token[64] = '\0';
    
    // Validate the token only contains hex characters
    for (int i = 0; i < 64; i++) {
        if (!isxdigit(token[i])) {
            fprintf(stderr, "Generated token contains non-hex characters\n");
            return NULL;
        }
    }
    
    return token;
}

// Helper function to create verification token
static char* createVerificationToken(ServerContext *ctx, Arena *arena, const char *userId) {
    char *token = generateToken(arena);
    if (!token) {
        return NULL;
    }
    
    // Insert verification token with expiration
    const char *values[] = {userId, token};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO email_verifications (user_id, token, expires_at) "
        "VALUES ($1, $2, NOW() + INTERVAL '24 hours') RETURNING token",
        values, 2);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Failed to create verification token\n");
        if (result) PQclear(result);
        return NULL;
    }

    PQclear(result);
    return token;
}

enum MHD_Result handleVerifyEmailRequest(ServerContext *ctx, struct MHD_Connection *connection, const char *token) {
    if (!token) {
        return redirectWithError(connection, "/verify-email", "invalid-token");
    }

    // Look up valid verification token
    const char *values[] = {token};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT v.id, v.user_id, v.verified_at, u.login "
        "FROM email_verifications v "
        "JOIN users u ON u.id = v.user_id "
        "WHERE v.token = $1 "
        "AND v.expires_at > NOW() "
        "AND v.verified_at IS NULL",
        values, 1);

    if (!result || PQntuples(result) == 0) {
        if (result) PQclear(result);
        return redirectWithError(connection, "/verify-email", "invalid-token");
    }

    // Get user info and store in arena
    char *verificationId = arenaDupString(ctx->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Mark email as verified
    const char *updateValues[] = {verificationId};
    result = executeParameterizedQuery(ctx->db,
        "UPDATE email_verifications "
        "SET verified_at = NOW() "
        "WHERE id = $1",
        updateValues, 1);

    if (!result) {
        return redirectWithError(connection, "/verify-email", "server-error");
    }
    PQclear(result);

    // Create empty response for redirect with success
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", "/verify-email?success=verified");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

enum MHD_Result handleResendVerificationRequest(ServerContext *ctx, struct MHD_Connection *connection) {
    // Get current user from session
    json_t *user = getUser(ctx, connection);
    if (!user) {
        return redirectWithError(connection, "/verify-email", "server-error");
    }

    const char *userId = json_string_value(json_object_get(user, "id"));
    const char *userEmail = json_string_value(json_object_get(user, "login"));
    
    // Check if already verified
    const char *values[] = {userId};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT id FROM email_verifications "
        "WHERE user_id = $1 "
        "AND verified_at IS NOT NULL",
        values, 1);

    if (result && PQntuples(result) > 0) {
        PQclear(result);
        return redirectWithError(connection, "/verify-email", "already-verified");
    }
    if (result) PQclear(result);

    // Create new verification token
    char *token = createVerificationToken(ctx, ctx->arena, userId);
    if (!token) {
        return redirectWithError(connection, "/verify-email", "server-error");
    }

    // Build verification URL
    char verificationUrl[512];
    size_t base_url_len = strlen("http://localhost:8080/verify-email?token=");
    size_t token_len = token ? strlen(token) : 0;

    if (base_url_len + token_len >= sizeof(verificationUrl)) {
        fprintf(stderr, "Token too long for verification URL buffer\n");
        return redirectWithError(connection, "/verify-email", "server-error");
    }
    snprintf(verificationUrl, sizeof(verificationUrl), "http://localhost:8080/verify-email?token=%s", token);

    // Send verification email using new email service
    if (sendVerificationEmail(ctx, ctx->arena, userEmail, verificationUrl) < 0) {
        return redirectWithError(connection, "/verify-email", "server-error");
    }

    // Redirect with success message
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", "/verify-email?success=sent");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

enum MHD_Result handleForgotPasswordRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;

    // Get form data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "login") == 0) {
            login = value;
        }
    }

    if (!login) {
        return redirectWithError(connection, "/forgot-password", "missing-fields");
    }

    // Look up user by email
    const char *values[] = {login};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT id FROM users WHERE login = $1",
        values, 1);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        if (result) PQclear(result);
        return redirectWithError(connection, "/forgot-password", "server-error");
    }

    // If user exists, create reset token and send email
    if (PQntuples(result) > 0) {
        char *userId = arenaDupString(ctx->arena, PQgetvalue(result, 0, 0));
        PQclear(result);

        char *token = createPasswordResetToken(ctx, ctx->arena, userId);
        if (!token) {
            return redirectWithError(connection, "/forgot-password", "server-error");
        }

        // Build reset URL
        char resetUrl[512];
        size_t base_url_len = strlen("http://localhost:8080/reset-password?token=");
        size_t token_len = token ? strlen(token) : 0;

        if (base_url_len + token_len >= sizeof(resetUrl)) {
            fprintf(stderr, "Token too long for reset URL buffer\n");
            return redirectWithError(connection, "/forgot-password", "server-error");
        }

        snprintf(resetUrl, sizeof(resetUrl), "http://localhost:8080/reset-password?token=%s", token);

        // Send password reset email using new email service
        if (sendPasswordResetEmail(ctx, ctx->arena, login, resetUrl) < 0) {
            return redirectWithError(connection, "/forgot-password", "server-error");
        }
    } else {
        // Even if user doesn't exist, pretend we sent email for security
        PQclear(result);
    }

    // Always redirect to success to prevent email enumeration
    struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", "/forgot-password?success=sent");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

enum MHD_Result handleResetPasswordRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *token = NULL;
    const char *password = NULL;
    const char *confirmPassword = NULL;

    // Get form data
    for (size_t i = 0; i < post->post_data.value_count; i++) {
        const char *key = post->post_data.keys[i];
        const char *value = post->post_data.values[i];
        if (strcmp(key, "token") == 0) {
            token = value;
        } else if (strcmp(key, "password") == 0) {
            password = value;
        } else if (strcmp(key, "confirm_password") == 0) {
            confirmPassword = value;
        }
    }

    if (!token || !password || !confirmPassword) {
        return redirectWithError(connection, "/reset-password", "missing-fields");
    }

    if (strcmp(password, confirmPassword) != 0) {
        char redirectBuffer[256];
        snprintf(redirectBuffer, sizeof(redirectBuffer), "/reset-password?token=%s&error=password-mismatch", token);
        char *redirectUrl = arenaDupString(ctx->arena, redirectBuffer);
        struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Location", redirectUrl);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Look up valid reset token
    const char *values[] = {token};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT user_id FROM password_resets "
        "WHERE token = $1 AND expires_at > NOW() AND used_at IS NULL",
        values, 1);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) == 0) {
        if (result) PQclear(result);
        return redirectWithError(connection, "/reset-password", "invalid-token");
    }

    char *userId = arenaDupString(ctx->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Get salt from website config
    const char *salt = NULL;
    if (ctx->website && ctx->website->auth) {
        salt = resolveString(ctx->arena, &ctx->website->auth->salt);
    }
    
    if (!salt) {
        fprintf(stderr, "No salt configured in website auth block\n");
        return redirectWithError(connection, "/reset-password", "server-error");
    }

    // Hash new password
    char *hashedPassword = hashPassword(ctx->arena, password, salt);
    if (!hashedPassword) {
        return redirectWithError(connection, "/reset-password", "server-error");
    }

    // Update password and mark token as used in a transaction
    const char *updateValues[] = {hashedPassword, userId, token};
    result = executeParameterizedQuery(ctx->db,
        "WITH updated_user AS ("
        "  UPDATE users SET password_hash = $1 "
        "  WHERE id = $2 "
        "  RETURNING id"
        ")"
        "UPDATE password_resets SET used_at = NOW() "
        "WHERE token = $3 "
        "RETURNING user_id",
        updateValues, 3);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) == 0) {
        if (result) PQclear(result);
        return redirectWithError(connection, "/reset-password", "server-error");
    }
    PQclear(result);

    // Redirect to login with success message
    struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Location", "/login?success=password-reset");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}
