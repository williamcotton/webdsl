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

// Create Lua state with request context
lua_State* createLuaState(json_t *requestContext);

// Convert between Lua and JSON
void pushJsonToLua(lua_State *L, json_t *json);
json_t* luaToJson(lua_State *L, int index);

// Extract values from Lua state
void extractLuaValues(lua_State *L, Arena *arena, const char ***values, size_t *value_count);

// High level handlers for pre/post filters
char* handleLuaPreFilter(Arena *arena, json_t *requestContext, const char *luaScript,
                        const char ***values, size_t *value_count);
char* handleLuaPostFilter(Arena *arena, json_t *jsonData, json_t *requestContext, 
                         const char *luaScript);

#endif
