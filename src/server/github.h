#ifndef SERVER_GITHUB_H
#define SERVER_GITHUB_H

#include "server.h"
#include "handler.h"

// GitHub OAuth handlers
enum MHD_Result handleGithubAuthRequest(ServerContext *ctx, struct MHD_Connection *connection);
enum MHD_Result handleGithubCallback(ServerContext *ctx, struct MHD_Connection *connection, struct PostContext *post);


#endif // SERVER_GITHUB_H
