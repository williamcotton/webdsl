#include "validation.h"
#include "../stringbuilder.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

static bool validateUrl(const char *url) {
    if (!url) return false;
    
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
