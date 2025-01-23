#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

#include "server.h"
#include "handler.h"

enum MHD_Result handleRegisterRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);

enum MHD_Result handleLogoutRequest(struct MHD_Connection *connection);

enum MHD_Result handleLoginRequest(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);

#endif /* SERVER_AUTH_H */
