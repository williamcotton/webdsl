#ifndef LUA_H
#define LUA_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "../ast.h"
#include "../arena.h"
#include "server.h"

// Initialize Lua subsystem
bool initLua(ServerContext *ctx);

// Clean up Lua subsystem
void cleanupLua(void);

// Execute a Lua pipeline step
json_t* executeLuaStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *ctx);

#endif
