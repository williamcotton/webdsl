#ifndef value_h
#define value_h

#include <stdbool.h>
#include <stdint.h>
#include "arena.h"

typedef enum {
    VALUE_NULL,
    VALUE_STRING,
    VALUE_NUMBER,
    VALUE_ENV_VAR
} ValueType;

typedef struct {
    ValueType type;
    uint64_t : 32;
    union {
        char *string;
        int number;
        char *envVarName;  // Stores name without the $ prefix
    } as;
} Value;

// Value creation functions
Value makeNull(void);
Value makeString(Arena *arena, const char *string);
Value makeNumber(int number);
Value makeEnvVar(Arena *arena, const char *name);  // Pass name without the $ prefix

// Value resolution functions
char* resolveString(Arena *arena, const Value* value);  // Returns allocated string or NULL
bool resolveNumber(const Value* value, int* out);  // Returns success/failure

#endif
