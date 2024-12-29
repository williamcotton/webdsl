#ifndef SERVER_VALIDATION_H
#define SERVER_VALIDATION_H

#include "../ast.h"
#include "../arena.h"
#include <stdbool.h>

// Validate a field value against its field definition
char* validateField(Arena *arena, const char *value, ApiField *field);

#endif // SERVER_VALIDATION_H
