#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop

// Generate JSON error response
json_t* generateErrorJson(const char *errorMessage);

// FNV-1a hash function - integer overflow is intentional
uint32_t hashString(const char *str) __attribute__((no_sanitize("unsigned-integer-overflow")));

#endif
