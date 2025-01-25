#ifndef SERVER_EMAIL_H
#define SERVER_EMAIL_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "server.h"

// Initialize email service with server context
void initEmail(ServerContext *ctx);

// Send verification email using configured template
int sendVerificationEmail(ServerContext *ctx, Arena *arena, const char *email, const char *verificationUrl);

// Send password reset email using configured template
int sendPasswordResetEmail(ServerContext *ctx, Arena *arena, const char *email, const char *resetUrl);

#endif // SERVER_EMAIL_H
