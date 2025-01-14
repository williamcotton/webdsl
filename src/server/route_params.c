#include "route_params.h"
#include <string.h>
#include <stdbool.h>

bool parseRouteParams(const char *pattern, const char *url, RouteParams *params, Arena *arena) {
    params->count = 0;
    
    const char *p = pattern;
    const char *u = url;
    
    // Iterate through pattern and URL simultaneously
    while (*p && *u) {
        if (*p == ':') {
            // Found a parameter, extract name
            p++; // Skip ':'
            const char *name_start = p;
            while (*p && *p != '/') p++;
            size_t name_len = p - name_start;
            
            // Extract value from URL
            const char *value_start = u;
            while (*u && *u != '/') u++;
            size_t value_len = u - value_start;
            
            // Store parameter if we have space
            if (params->count < MAX_ROUTE_PARAMS) {
                char *name = arenaAlloc(arena, name_len + 1);
                char *value = arenaAlloc(arena, value_len + 1);
                
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                memcpy(value, value_start, value_len);
                value[value_len] = '\0';
                
                params->params[params->count].name = name;
                params->params[params->count].value = value;
                params->count++;
            }
        } else if (*p == *u) {
            // Characters match, continue
            p++;
            u++;
        } else {
            // Mismatch
            return false;
        }
    }
    
    // Both strings should be fully consumed for a match
    return *p == '\0' && *u == '\0';
}
