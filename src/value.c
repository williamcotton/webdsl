#include "value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Value makeNull(void) {
    Value value;
    value.type = VALUE_NULL;
    return value;
}

Value makeString(Arena *arena, const char *string) {
    Value value;
    value.type = VALUE_STRING;
    value.as.string = string ? arenaDupString(arena, string) : NULL;
    return value;
}

Value makeNumber(int number) {
    Value value;
    value.type = VALUE_NUMBER;
    value.as.number = number;
    return value;
}

Value makeEnvVar(Arena *arena, const char *name) {
    Value value;
    value.type = VALUE_ENV_VAR;
    value.as.envVarName = name ? arenaDupString(arena, name) : NULL;
    return value;
}

char* resolveString(Arena *arena, const Value* value) {
    if (!value) return NULL;
    
    switch (value->type) {
        case VALUE_STRING:
            return value->as.string ? arenaDupString(arena, value->as.string) : NULL;
        case VALUE_NUMBER: {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", value->as.number);
            return arenaDupString(arena, buffer);
        }
        case VALUE_ENV_VAR: {
            if (!value->as.envVarName) return NULL;
            const char* env = getenv(value->as.envVarName);
            return env ? arenaDupString(arena, env) : NULL;
        }
        case VALUE_NULL:
            return NULL;
    }
    return NULL;  // Handle any unhandled cases
}

bool resolveNumber(const Value* value, int* out) {
    if (!value || !out) return false;
    
    switch (value->type) {
        case VALUE_NUMBER:
            *out = value->as.number;
            return true;
        case VALUE_STRING:
            if (!value->as.string) return false;
            char* endptr;
            *out = (int)strtol(value->as.string, &endptr, 10);
            return *endptr == '\0';  // Successful only if entire string was parsed
        case VALUE_ENV_VAR: {
            if (!value->as.envVarName) return false;
            const char* env = getenv(value->as.envVarName);
            if (!env) return false;
            char* endptr2;
            *out = (int)strtol(env, &endptr2, 10);
            return *endptr2 == '\0';  // Successful only if entire string was parsed
        }
        case VALUE_NULL:
        default:
            return false;
    }
}
