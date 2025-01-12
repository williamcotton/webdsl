#ifndef INCLUDE_H
#define INCLUDE_H

#include "parser.h"
#include "ast.h"
#include "arena.h"

typedef struct IncludeState {
    char **included_files;  // Array of already included files
    int num_included;       // Number of included files
    int max_includes;       // Maximum allowed includes
} IncludeState;

// Initialize include state
void initIncludeState(IncludeState *state);

// Process an include statement
bool processInclude(Parser *parser, WebsiteNode *website, 
                    const char *filepath, IncludeState *state);

#endif // INCLUDE_H 
