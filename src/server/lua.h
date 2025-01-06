#ifndef LUA_H
#define LUA_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "../arena.h"
#include "../ast.h"

json_t *executeLuaStep(PipelineStepNode *step, json_t *input,
                       json_t *requestContext, Arena *arena);

#endif
