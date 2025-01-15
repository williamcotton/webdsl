#ifndef SERVER_VALIDATION_H
#define SERVER_VALIDATION_H

#include "../ast.h"
#include "../arena.h"
#include <stdbool.h>
#include <regex.h>
#include <jansson.h>
#include "handler.h"

// Common format types
#define FORMAT_EMAIL "email"
#define FORMAT_URL "url"
#define FORMAT_DATE "date"
#define FORMAT_PHONE "phone"
#define FORMAT_UUID "uuid"
#define FORMAT_IPV4 "ipv4"
#define FORMAT_TIME "time"
#define FORMAT_DATETIME "datetime"

// Validate a field value against its field definition
char* validateField(Arena *arena, const char *value, ApiField *field);

// New validation functions
json_t* validateJsonFields(Arena *arena, ApiField *fields, struct PostContext *post_ctx);
json_t* validateFormFields(Arena *arena, ApiField *fields, struct PostContext *post_ctx);

#endif // SERVER_VALIDATION_H
