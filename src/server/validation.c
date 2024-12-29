#include "validation.h"
#include "../stringbuilder.h"
#include <string.h>
#include <ctype.h>

static bool validateEmail(const char *email) {
    if (!email) return false;
    
    // Basic email validation: must contain @ and at least one . after @
    const char *at = strchr(email, '@');
    if (!at) return false;
    
    const char *dot = strchr(at, '.');
    if (!dot || dot == at + 1) return false;
    
    // Must have characters before @ and after last .
    if (at == email || !dot[1]) return false;
    
    return true;
}

static bool validateLength(const char *value, int minLength, int maxLength) {
    if (!value) return false;
    size_t len = strlen(value);
    return len >= (size_t)minLength && len <= (size_t)maxLength;
}

static bool validateNumber(const char *value) {
    if (!value) return false;
    
    // Skip leading whitespace
    while (isspace(*value)) value++;
    
    // Handle optional sign
    if (*value == '-' || *value == '+') value++;
    
    // Must have at least one digit
    if (!isdigit(*value)) return false;
    
    // Check remaining characters are digits
    while (*value) {
        if (!isdigit(*value)) return false;
        value++;
    }
    
    return true;
}

char* validateField(Arena *arena, const char *value, ApiField *field) {
    if (!value) {
        if (field->required) {
            return arenaDupString(arena, "Field is required");
        }
        return NULL; // Optional field can be null
    }

    // Type validation
    if (strcmp(field->type, "string") == 0) {
        if (field->minLength > 0 || field->maxLength > 0) {
            if (!validateLength(value, field->minLength, field->maxLength)) {
                StringBuilder *sb = StringBuilder_new(arena);
                StringBuilder_append(sb, "Length must be between %d and %d characters", 
                    field->minLength, field->maxLength);
                return arenaDupString(arena, StringBuilder_get(sb));
            }
        }
        
        if (field->format && strcmp(field->format, "email") == 0) {
            if (!validateEmail(value)) {
                return arenaDupString(arena, "Invalid email format");
            }
        }
    }
    else if (strcmp(field->type, "number") == 0) {
        if (!validateNumber(value)) {
            return arenaDupString(arena, "Must be a valid number");
        }
    }
    
    return NULL; // Validation passed
}
