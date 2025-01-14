#ifndef SERVER_ROUTE_PARAMS_H
#define SERVER_ROUTE_PARAMS_H

#include "../arena.h"

#define MAX_ROUTE_PARAMS 8

// Structure to hold a single route parameter
typedef struct RouteParam {
    const char *name;   // Parameter name (e.g., "id" from ":id")
    const char *value;  // Parameter value (e.g., "123" from "/notes/123")
} RouteParam;

// Structure to hold all route parameters
typedef struct RouteParams {
    RouteParam params[MAX_ROUTE_PARAMS];
    int count;
} RouteParams;

// Parse route parameters from a URL based on a pattern
// Returns true if the URL matches the pattern, false otherwise
// Parameters are allocated from the provided arena
bool parseRouteParams(const char *pattern, const char *url, RouteParams *params, Arena *arena);

#endif // SERVER_ROUTE_PARAMS_H
