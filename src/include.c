#include "include.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INCLUDES 100  // Maximum number of includes to prevent infinite recursion

void initIncludeState(IncludeState *state) {
    state->included_files = malloc(sizeof(char*) * MAX_INCLUDES);
    state->num_included = 0;
    state->max_includes = MAX_INCLUDES;
}

static bool isFileIncluded(IncludeState *state, const char *filepath) {
    for (int i = 0; i < state->num_included; i++) {
        if (strcmp(state->included_files[i], filepath) == 0) {
            return true;
        }
    }
    return false;
}

static char *readFile(const char *filepath, Arena *arena) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    // Allocate buffer
    char *buffer = arenaAlloc(arena, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    // Read file
    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    buffer[bytes_read] = '\0';
    fclose(file);

    return buffer;
}

bool processInclude(Parser *parser, WebsiteNode *website, 
                   const char *filepath, IncludeState *state) {
    (void)website;  // Unused for now

    // Check for circular includes
    if (isFileIncluded(state, filepath)) {
        fprintf(stderr, "Error: Circular include detected for file '%s'\n", filepath);
        return false;
    }

    // Check max includes
    if (state->num_included >= state->max_includes) {
        fprintf(stderr, "Error: Maximum include depth exceeded\n");
        return false;
    }

    // Read file
    char *content = readFile(filepath, parser->arena);
    if (!content) {
        fprintf(stderr, "Error: Could not read file '%s'\n", filepath);
        return false;
    }

    // Add to included files
    state->included_files[state->num_included] = strdup(filepath);
    state->num_included++;

    // Parse included content
    Parser includeParser;
    initParser(&includeParser, content);
    
    // TODO: Parse and merge content
    // For now, just return true to get the basic structure working
    return true;
} 
