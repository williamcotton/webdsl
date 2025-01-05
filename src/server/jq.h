#ifndef SERVER_JQ_H
#define SERVER_JQ_H

#include <jq.h>
#include <jansson.h>
#include "../arena.h"
#include "routing.h"

// Process a JQ filter on JSON input
jv processJqFilter(jq_state *jq, json_t *input_json);

// Convert between Jansson and JQ JSON formats
jv janssonToJv(json_t *json);
json_t* jvToJansson(jv value);

// Execute a JQ pipeline step
json_t* executeJqStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena);

#endif // SERVER_JQ_H
