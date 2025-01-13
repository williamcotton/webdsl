#include "pipeline_executor.h"
#include <string.h>

json_t* executePipelineStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *ctx) {
    if (!step || !step->execute) {
        return NULL;
    }

    // Check for existing error here instead of in each executor
    json_t *error = json_object_get(input, "error");
    if (error) {
        return json_deep_copy(input);
    }

    return step->execute(step, input, requestContext, arena, ctx);
}

json_t* executePipeline(ServerContext *ctx, PipelineStepNode *pipeline, json_t *requestContext, Arena *arena) {
    if (!pipeline) {
        return NULL;
    }

    json_t *current = requestContext;
    PipelineStepNode *step = pipeline;
    
    while (step) {
        json_t *result = executePipelineStep(step, current, requestContext, arena, ctx);
        if (current != requestContext) {
            json_decref(current);
        }
        if (!result) {
            return NULL;
        }
        current = result;
        step = step->next;
    }
    
    return current;
}

json_t* createRequestContext(const char *method, const char *url, json_t *query, json_t *headers, json_t *cookies, json_t *body) {
    json_t *context = json_object();
    
    // Add method and url
    json_object_set_new(context, "method", json_string(method ? method : "GET"));
    json_object_set_new(context, "url", json_string(url ? url : "/"));
    
    // Add or create empty objects for other fields
    json_object_set_new(context, "query", query ? json_deep_copy(query) : json_object());
    json_object_set_new(context, "headers", headers ? json_deep_copy(headers) : json_object());
    json_object_set_new(context, "cookies", cookies ? json_deep_copy(cookies) : json_object());
    json_object_set_new(context, "body", body ? json_deep_copy(body) : json_object());
    
    return context;
}
