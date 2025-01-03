#ifndef UTILS_H
#define UTILS_H

#include <jansson.h>

// Generate JSON error response
char* generateErrorJson(const char *errorMessage);

// FNV-1a hash function - integer overflow is intentional
uint32_t hashString(const char *str) __attribute__((no_sanitize("unsigned-integer-overflow")));

#endif
