#ifndef JQ_WRAPPER_H
#define JQ_WRAPPER_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include <jq.h>
#include <jv.h>
#pragma clang diagnostic pop
#include "arena.h"

// Apply a JQ filter to a JSON string
// Returns NULL on error
char* applyJqFilter(Arena *arena, const char *json, const char *filter);

#endif // JQ_WRAPPER_H
