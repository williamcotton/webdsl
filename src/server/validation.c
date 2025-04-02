#include "validation.h"
#include "../stringbuilder.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <regex.h>

static bool validateEmail(const char *email) {
    if (!email) return false;
    
    // Basic email validation: must contain @ and at least one . after @
    const char *at = strchr(email, '@');
    if (!at) return false;
    
    const char *dot = strchr(at, '.');
    if (!dot || dot == at + 1) return false;
    
    // Must have characters before @ and after last .
    if (at == email || !dot[1]) return false;
    
    // Add maximum length validation
    if (strlen(email) > 254) return false;
    
    // Add check for illegal characters
    for (const char *p = email; *p; p++) {
        if (!(isalnum(*p) || *p == '@' || *p == '.' || *p == '-' || 
              *p == '_' || *p == '+')) {
            return false;
        }
    }
    
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

static bool validateUrl(const char *url) {
    if (!url) return false;
    
    // Maximum reasonable URL length
    if (strlen(url) > 2048) return false;
    
    // Basic URL validation
    if (strncmp(url, "http://", 7) != 0 && 
        strncmp(url, "https://", 8) != 0) {
        return false;
    }
    
    // Must have something after protocol
    const char *domain = strchr(url, '/') + 2;
    if (!*domain) return false;
    
    // Must have at least one dot in domain
    if (!strchr(domain, '.')) return false;
    
    // Check for illegal characters
    for (const char *p = url; *p; p++) {
        if (*p <= 0x20 || *p > 0x7E) {  // Non-printable or non-ASCII
            return false;
        }
    }
    
    return true;
}

static bool validateDate(const char *date) {
    if (!date) return false;
    
    // Format: YYYY-MM-DD
    if (strlen(date) != 10) return false;
    if (date[4] != '-' || date[7] != '-') return false;
    
    int year = atoi(date);
    int month = atoi(date + 5);
    int day = atoi(date + 8);
    
    return year >= 1900 && year <= 9999 &&
           month >= 1 && month <= 12 &&
           day >= 1 && day <= 31;
}

static bool validateTime(const char *time) {
    if (!time) return false;
    
    // Format: HH:MM or HH:MM:SS
    size_t len = strlen(time);
    if (len != 5 && len != 8) return false;
    if (time[2] != ':' || (len == 8 && time[5] != ':')) return false;
    
    int hour = atoi(time);
    int minute = atoi(time + 3);
    int second = len == 8 ? atoi(time + 6) : 0;
    
    return hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}

static bool validatePhone(const char *phone) {
    if (!phone) return false;
    
    // Allow +, spaces, (), - in phone numbers
    size_t digits = 0;
    for (const char *p = phone; *p; p++) {
        if (isdigit(*p)) digits++;
        else if (!strchr("+ ()-", *p)) return false;
    }
    
    // Most phone numbers are between 7 and 15 digits
    return digits >= 7 && digits <= 15;
}

static bool validateUuid(const char *uuid) {
    if (!uuid) return false;
    if (strlen(uuid) != 36) return false;
    
    // Format: 8-4-4-4-12 hexadecimal digits
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else if (!isxdigit(uuid[i])) return false;
    }
    
    return true;
}

static bool validateIpv4(const char *ip) {
    if (!ip) return false;
    
    int nums = 0, dots = 0;
    char buf[4] = {0};
    int bufpos = 0;
    
    for (const char *p = ip; *p; p++) {
        if (*p == '.') {
            dots++;
            buf[bufpos] = '\0';
            int num = atoi(buf);
            if (num < 0 || num > 255) return false;
            nums++;
            bufpos = 0;
        } else if (isdigit(*p)) {
            if (bufpos >= 3) return false;
            buf[bufpos++] = *p;
        } else return false;
    }
    
    // Handle last number
    if (bufpos > 0) {
        buf[bufpos] = '\0';
        int num = atoi(buf);
        if (num < 0 || num > 255) return false;
        nums++;
    }
    
    return nums == 4 && dots == 3;
}

static bool validatePattern(const char *value, const char *pattern) {
    if (!value || !pattern) return false;
    
    regex_t regex;
    int reti;
    bool result = false;
    
    // Compile regex with extended regex support
    reti = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
    if (reti) {
        return false;  // Failed to compile regex
    }
    
    // Execute regex match
    reti = regexec(&regex, value, 0, NULL, 0);
    if (!reti) {
        result = true;  // Match
    }
    
    // Free compiled regex - must be done even if match fails
    regfree(&regex);
    return result;
}

char* validateField(Arena *arena, const char *value, ApiField *field) {
    if (!value) {
        if (field->required) {
            return arenaDupString(arena, "Field is required");
        }
        return NULL;
    }

    if (strcmp(field->type, "string") == 0) {
        if (field->minLength > 0 || field->maxLength > 0) {
            if (!validateLength(value, field->minLength, field->maxLength)) {
                StringBuilder *sb = StringBuilder_new(arena);
                StringBuilder_append(sb, "Length must be between %d and %d characters", 
                    field->minLength, field->maxLength);
                return arenaDupString(arena, StringBuilder_get(sb));
            }
        }
        
        if (field->format) {
            bool valid = true;
            const char *error = NULL;
            
            if (strcmp(field->format, FORMAT_EMAIL) == 0) {
                valid = validateEmail(value);
                error = "Invalid email format";
            }
            else if (strcmp(field->format, FORMAT_URL) == 0) {
                valid = validateUrl(value);
                error = "Invalid URL format";
            }
            else if (strcmp(field->format, FORMAT_DATE) == 0) {
                valid = validateDate(value);
                error = "Invalid date format (use YYYY-MM-DD)";
            }
            else if (strcmp(field->format, FORMAT_PHONE) == 0) {
                valid = validatePhone(value);
                error = "Invalid phone number";
            }
            else if (strcmp(field->format, FORMAT_UUID) == 0) {
                valid = validateUuid(value);
                error = "Invalid UUID format";
            }
            else if (strcmp(field->format, FORMAT_IPV4) == 0) {
                valid = validateIpv4(value);
                error = "Invalid IPv4 address";
            }
            else if (strcmp(field->format, FORMAT_TIME) == 0) {
                valid = validateTime(value);
                error = "Invalid time format (use HH:MM or HH:MM:SS)";
            }
            
            if (!valid) {
                return arenaDupString(arena, error);
            }
        }
        
        if (field->validate.match.pattern) {
            if (!validatePattern(value, field->validate.match.pattern)) {
                StringBuilder *sb = StringBuilder_new(arena);
                StringBuilder_append(sb, "Value must match pattern: %s", 
                    field->validate.match.pattern);
                return arenaDupString(arena, StringBuilder_get(sb));
            }
        }
    }
    else if (strcmp(field->type, "number") == 0) {
        if (!validateNumber(value)) {
            return arenaDupString(arena, "Must be a valid number");
        }
        
        // Check number range if specified
        int num = atoi(value);
        if (field->validate.range.min != 0 || field->validate.range.max != 0) {
            if (num < field->validate.range.min || num > field->validate.range.max) {
                StringBuilder *sb = StringBuilder_new(arena);
                StringBuilder_append(sb, "Number must be between %d and %d",
                    field->validate.range.min, field->validate.range.max);
                return arenaDupString(arena, StringBuilder_get(sb));
            }
        }
    }
    
    return NULL;
}

json_t* validateJsonFields(Arena *arena, ApiField *fields, struct PostContext *post_ctx) {
    if (!post_ctx->raw_json) {
        json_t *error = json_object();
        json_object_set_new(error, "error", json_string("No JSON data provided"));
        return error;
    }

    // Parse JSON data
    json_error_t json_error;
    json_t *json_body = json_loads(post_ctx->raw_json, 0, &json_error);
    if (!json_body) {
        json_t *error = json_object();
        json_object_set_new(error, "error", json_string("Invalid JSON format"));
        return error;
    }

    // Create errors object
    json_t *errors = json_object();
    bool has_errors = false;

    // Validate each field
    for (ApiField *field = fields; field; field = field->next) {
        const char *value = NULL;
        json_t *json_value = json_object_get(json_body, field->name);
        
        if (json_value) {
            if (json_is_string(json_value)) {
                value = json_string_value(json_value);
            } else if (json_is_number(json_value)) {
                char num_str[32];
                snprintf(num_str, sizeof(num_str), "%.0f", json_number_value(json_value));
                value = arenaDupString(arena, num_str);
            }
        }

        char *error = validateField(arena, value, field);
        if (error) {
            json_object_set_new(errors, field->name, json_string(error));
            has_errors = true;
        }
    }

    json_decref(json_body);

    if (has_errors) {
        json_t *error_response = json_object();
        json_object_set_new(error_response, "errors", errors);
        return error_response;
    }

    json_decref(errors);
    return NULL;
}

json_t* validateFormFields(Arena *arena, ApiField *fields, struct PostContext *post_ctx) {
    // Create errors object
    json_t *errors = json_object();
    bool has_errors = false;

    // Validate each field
    for (ApiField *field = fields; field; field = field->next) {
        const char *value = NULL;
        
        // Find field value in post data
        for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
            if (strcmp(post_ctx->post_data.keys[i], field->name) == 0) {
                value = post_ctx->post_data.values[i];
                break;
            }
        }

        char *error = validateField(arena, value, field);
        if (error) {
            json_object_set_new(errors, field->name, json_string(error));
            has_errors = true;
        }
    }

    if (has_errors) {
        json_t *error_response = json_object();
        json_object_set_new(error_response, "errors", errors);
        
        // Add form values to response for re-rendering
        json_t *values = json_object();
        for (size_t i = 0; i < post_ctx->post_data.value_count; i++) {
            json_object_set_new(values, post_ctx->post_data.keys[i], 
                              json_string(post_ctx->post_data.values[i]));
        }
        json_object_set_new(error_response, "values", values);
        
        return error_response;
    }

    json_decref(errors);
    return NULL;
}
