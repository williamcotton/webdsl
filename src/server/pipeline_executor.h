#ifndef SERVER_PIPELINE_EXECUTOR_H
#define SERVER_PIPELINE_EXECUTOR_H

#include "../ast.h"
#include "../arena.h"
#include "server.h"
#include <jansson.h>

// Pipeline step executor setup
void setupStepExecutor(PipelineStepNode *step);

// Execute a single pipeline step
json_t* executePipelineStep(
    PipelineStepNode *step,
    json_t *input,
    json_t *requestContext,
    Arena *arena,
    ServerContext *ctx
);

// Execute a full pipeline with request context
json_t* executePipeline(
    ServerContext *ctx,
    PipelineStepNode *pipeline,
    json_t *requestContext,
    Arena *arena
);

// Create a basic request context with default values
json_t* createRequestContext(
    const char *method,
    const char *url,
    json_t *query,
    json_t *headers,
    json_t *cookies,
    json_t *body
);

#endif // SERVER_PIPELINE_EXECUTOR_H
