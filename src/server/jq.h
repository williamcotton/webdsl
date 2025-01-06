#ifndef SERVER_JQ_H
#define SERVER_JQ_H

#include <jq.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "../arena.h"
#include "../ast.h"

// Execute a JQ pipeline step
json_t* executeJqStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena);

#endif // SERVER_JQ_H
