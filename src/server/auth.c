#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#include <argon2.h>
#pragma clang diagnostic pop
#include "server.h"
#include "db.h"
#include "auth.h"
#include <microhttpd.h>

static bool verifyPassword(const char *password, const char *storedHash) {
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // Same fixed salt as in hashPassword
    
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
    char hashStr[65];
    for (size_t i = 0; i < sizeof(hash); i++) {
        snprintf(&hashStr[i * 2], 3, "%02x", hash[i]);
    }
    hashStr[64] = '\0';
    
    return strcmp(hashStr, storedHash) == 0;
}

static char* createSession(ServerContext *ctx, Arena *arena, const char *userId) {
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
    char token[65];
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

static char* urlEncode(Arena *arena, const char *str) {
    const char *hex = "0123456789ABCDEF";
    const char *p;
    char *buf = arenaAlloc(arena, strlen(str) * 3 + 1);
    char *pbuf = buf;
    
    for (p = str; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            *pbuf++ = *p;
        } else {
            *pbuf++ = '%';
            *pbuf++ = hex[(*p >> 4) & 15];
            *pbuf++ = hex[*p & 15];
        }
    }
    *pbuf = '\0';
    return buf;
}

static enum MHD_Result redirectWithError(struct MHD_Connection *connection,
                                         const char *location,
                                         const char *error,
                                         Arena *arena) {
    // URL encode the error message
    char *encoded_error = urlEncode(arena, error);

    // Create redirect URL with error parameter
    char redirect_url[512];
    snprintf(redirect_url, sizeof(redirect_url), "%s?error=%s", location, encoded_error);

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
        return redirectWithError(connection, "/login", "Email and password are required", post->arena);
    }

    // Look up user in database
    const char *values[] = {login};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "SELECT id, password_hash FROM users WHERE login = $1",
        values, 1);

    if (!result || PQntuples(result) == 0) {
        fprintf(stderr, "Login failed - user not found: %s\n", login);
        if (result) PQclear(result);
        return redirectWithError(connection, "/login", "Invalid email or password", post->arena);
    }

    // Get stored password hash and user ID
    char *storedHash = arenaDupString(post->arena, PQgetvalue(result, 0, 1));
    char *userId = arenaDupString(post->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Verify password
    if (!verifyPassword(password, storedHash)) {
        fprintf(stderr, "Login failed - incorrect password for: %s\n", login);
        return redirectWithError(connection, "/login", "Invalid email or password", post->arena);
    }

    printf("Login successful for user: %s (id: %s)\n", login, userId);
    
    // Create session
    char *token = createSession(ctx, post->arena, userId);
    if (!token) {
        return redirectWithError(connection, "/login", "Failed to create session", post->arena);
    }
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Set session cookie
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict", token);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Set redirect header
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

enum MHD_Result handleLogoutRequest(struct MHD_Connection *connection) {
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

static char* hashPassword(Arena *arena, const char *password) {
    uint8_t hash[32];
    uint8_t salt[16] = {0}; // In production, generate random salt
    
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
    
    return hashStr;
}

enum MHD_Result handleRegisterRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post) {
    const char *login = NULL;
    const char *password = NULL;
    const char *confirm_password = NULL;

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

    // Print registration attempt for now
    printf("Registration attempt - login: %s\n", login ? login : "null");
    
    // Basic validation
    if (!login || !password || !confirm_password) {
        // TODO: Return error page
        return MHD_NO;
    }
    
    if (strcmp(password, confirm_password) != 0) {
        // TODO: Return error page with "Passwords don't match"
        return MHD_NO;
    }

    // Hash password
    char *passwordHash = hashPassword(post->arena, password);
    printf("Password hash: %s\n", passwordHash);
    if (!passwordHash) {
        return MHD_NO;
    }

    // Insert user into database
    const char *values[] = {login, passwordHash};
    PGresult *result = executeParameterizedQuery(ctx->db,
        "INSERT INTO users (login, password_hash) VALUES ($1, $2) RETURNING id",
        values, 2);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Failed to create user account\n");
        if (result) PQclear(result);
        return MHD_NO;
    }

    // Get the new user's ID
    char *userId = arenaDupString(post->arena, PQgetvalue(result, 0, 0));
    PQclear(result);

    // Create session
    char *token = createSession(ctx, post->arena, userId);
    if (!token) {
        return MHD_NO;
    }
    
    // Create empty response for redirect
    struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    
    // Set session cookie
    char cookie[256];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict", token);
    MHD_add_response_header(response, "Set-Cookie", cookie);
    
    // Redirect to home page
    MHD_add_response_header(response, "Location", "/");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}
