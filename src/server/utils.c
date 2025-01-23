#include "utils.h"
#include <stdint.h>

json_t* generateErrorJson(const char *errorMessage) {
    json_t *root = json_object();
    json_object_set_new(root, "error", json_string(errorMessage));
    return root;
}

uint32_t hashString(const char *str) __attribute__((no_sanitize("unsigned-integer-overflow"))) {
    uint32_t hash = 2166136261u;
    for (const char *s = str; *s; s++) {
        hash ^= (uint32_t)*s;
        hash *= 16777619u;
    }
    return hash;
}
