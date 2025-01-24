#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

#include "server.h"
#include "handler.h"
#include <jansson.h>

// Get user from session token
json_t* getUser(ServerContext *ctx, struct MHD_Connection *connection);

enum MHD_Result handleRegisterRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);

enum MHD_Result handleLogoutRequest(ServerContext *ctx, struct MHD_Connection *connection);

enum MHD_Result handleLoginRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);

enum MHD_Result handleVerifyEmailRequest(ServerContext *ctx, struct MHD_Connection *connection, const char *token);

enum MHD_Result handleResendVerificationRequest(ServerContext *ctx, struct MHD_Connection *connection);

// New password reset handlers
enum MHD_Result handleForgotPasswordRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);

enum MHD_Result handleResetPasswordRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);


char* generateToken(Arena *arena);
bool storeOAuthCredentials(ServerContext *ctx, const char *userId, 
                                const char *provider, const char *providerId,
                                const char *accessToken, json_t *credentials);
enum MHD_Result redirectWithError(struct MHD_Connection *connection,
                                         const char *location,
                                         const char *error_key);
bool storeStateToken(const char *token);
bool validateStateToken(const char *token);
char* createSession(ServerContext *ctx, Arena *arena, const char *userId);

#endif /* SERVER_AUTH_H */
