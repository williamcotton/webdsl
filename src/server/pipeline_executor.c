#include "pipeline_executor.h"
#include "db.h"
#include "jq.h"
#include "lua.h"
#include <string.h>

// Function to set up the executor based on step type
void setupStepExecutor(PipelineStepNode *step) {
  switch (step->type) {
  case STEP_JQ:
    step->execute = executeJqStep;
    break;
  case STEP_LUA:
    step->execute = executeLuaStep;
    break;
  case STEP_SQL:
  case STEP_DYNAMIC_SQL:
    step->execute = executeSqlStep;
    break;
  }
}

json_t* executePipelineStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *ctx) {
    if (!step || !step->execute) {
        return NULL;
    }

    // Check for existing error or redirect here instead of in each executor
    json_t *error = json_object_get(input, "error");
    if (error) {
        return json_deep_copy(input);
    }

    json_t *redirect = json_object_get(input, "redirect");
    if (redirect) {
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
