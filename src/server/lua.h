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

// Create Lua state with request context
lua_State* createLuaState(json_t *requestContext, Arena *arena, bool loadQueryBuilder);

// Convert between Lua and JSON
void pushJsonToLua(lua_State *L, json_t *json);
json_t* luaToJson(lua_State *L, int index);

json_t *executeLuaStep(PipelineStepNode *step, json_t *input,
                       json_t *requestContext, Arena *arena);

#endif
