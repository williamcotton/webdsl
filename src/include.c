#include "include.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INCLUDES 100  // Maximum number of includes to prevent infinite recursion

bool initIncludeState(IncludeState *state) {
    state->included_files = malloc(sizeof(char*) * MAX_INCLUDES);
    if (state->included_files == NULL) {
        return false;
    }
    state->num_included = 0;
    state->max_includes = MAX_INCLUDES;
    return true;
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

bool processInclude(Parser *parser, WebsiteNode *website, const char *filepath, IncludeState *state) {
    // Check for circular includes
    for (int i = 0; i < state->num_included; i++) {
        if (strcmp(state->included_files[i], filepath) == 0) {
            fprintf(stderr, "Error: Circular include detected for file '%s'\n", filepath);
            return false;
        }
    }

    // Check maximum include depth
    if (state->num_included >= state->max_includes) {
        fprintf(stderr, "Error: Maximum include depth exceeded\n");
        return false;
    }

    // Read the file content
    char *content = readFile(filepath, parser->arena);
    if (!content) {
        fprintf(stderr, "Error: Failed to read file '%s'\n", filepath);
        return false;
    }

    // Add to included files list
    state->included_files[state->num_included] = strdup(filepath);
    state->num_included++;

    // Parse the included content
    Parser includeParser;
    initParser(&includeParser, content);
    Arena *tempArena = includeParser.arena;  // Store temporary arena
    includeParser.arena = parser->arena;  // Use the same arena as the main parser

    // Parse content and merge into main website
    WebsiteNode *included = parseWebsiteContent(&includeParser);
    
    // Free the temporary arena that was created
    freeArena(tempArena);

    if (!includeParser.hadError) {
        // Merge the nodes
        if (included->pageHead) {
            if (!website->pageHead) {
                website->pageHead = included->pageHead;
            } else {
                PageNode *current = website->pageHead;
                while (current->next) current = current->next;
                current->next = included->pageHead;
            }
        }
        
        if (included->layoutHead) {
            if (!website->layoutHead) {
                website->layoutHead = included->layoutHead;
            } else {
                LayoutNode *current = website->layoutHead;
                while (current->next) current = current->next;
                current->next = included->layoutHead;
            }
        }
        
        if (included->styleHead) {
            if (!website->styleHead) {
                website->styleHead = included->styleHead;
            } else {
                StyleBlockNode *current = website->styleHead;
                while (current->next) current = current->next;
                current->next = included->styleHead;
            }
        }
        
        if (included->queryHead) {
            if (!website->queryHead) {
                website->queryHead = included->queryHead;
            } else {
                QueryNode *current = website->queryHead;
                while (current->next) current = current->next;
                current->next = included->queryHead;
            }
        }
        
        if (included->transformHead) {
            if (!website->transformHead) {
                website->transformHead = included->transformHead;
            } else {
                TransformNode *current = website->transformHead;
                while (current->next) current = current->next;
                current->next = included->transformHead;
            }
        }
        
        if (included->scriptHead) {
            if (!website->scriptHead) {
                website->scriptHead = included->scriptHead;
            } else {
                ScriptNode *current = website->scriptHead;
                while (current->next) current = current->next;
                current->next = included->scriptHead;
            }
        }
        
        if (included->apiHead) {
            if (!website->apiHead) {
                website->apiHead = included->apiHead;
            } else {
                ApiEndpoint *current = website->apiHead;
                while (current->next) current = current->next;
                current->next = included->apiHead;
            }
        }

        if (included->partialHead) {
            if (!website->partialHead) {
                website->partialHead = included->partialHead;
            } else {
                PartialNode *current = website->partialHead;
                while (current->next) current = current->next;
                current->next = included->partialHead;
            }
        }
        
        return true;
    }

    fprintf(stderr, "Error: Failed to parse included file '%s'\n", filepath);
    return false;
} 
