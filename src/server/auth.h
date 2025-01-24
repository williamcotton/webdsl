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

#endif /* SERVER_AUTH_H */
