#ifndef UTILS_H
#define UTILS_H

#include <jansson.h>

// Generate JSON error response
char* generateErrorJson(const char *errorMessage);

#endif
